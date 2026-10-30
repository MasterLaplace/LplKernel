#include <qemu-plugin.h>

#include <glib.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

/**
 * @brief What one translated block costs each time it runs, on each vCPU.
 */
typedef struct BlockCounters {
    uint64_t executions; /**< Times the block ran. */
    uint64_t loads;      /**< Memory loads its instructions made. */
    uint64_t stores;     /**< Memory stores its instructions made. */
} BlockCounters;

/**
 * @brief A block of guest code, by the address it starts at, and its counters.
 */
typedef struct Block {
    uint64_t address;                        /**< Guest virtual address of its first instruction. */
    uint64_t instructions;                   /**< Instructions in it. */
    struct qemu_plugin_scoreboard *counters; /**< One BlockCounters per vCPU. */
    BlockCounters baseline;                  /**< The counters when the window last opened, summed. */
    BlockCounters counted;                   /**< What ran while the window was open, over all its openings. */
} Block;

static GMutex blocks_lock;
static GHashTable *blocks;
static char *output_path;
static uint64_t window_begin;
static uint64_t window_end;
static uint64_t window_inside;
static uint64_t window_end_executions = 1u;
static uint64_t window_end_seen;
static uint64_t window_skip;
static uint64_t window_skipped;
static uint64_t window_calls;
static gboolean window_skipping;
static gboolean window_open;
static gboolean window_closed;
static gboolean window_just_opened;
static uint64_t inside_return;
static struct qemu_plugin_register *stack_pointer;

static uint64_t block_sum(const Block *block, size_t offset)
{
    return qemu_plugin_u64_sum((qemu_plugin_u64){block->counters, offset});
}

static void resume_counting(void)
{
    GHashTableIter iterator;
    gpointer value;

    g_hash_table_iter_init(&iterator, blocks);
    while (g_hash_table_iter_next(&iterator, NULL, &value))
    {
        Block *block = value;
        block->baseline.executions = block_sum(block, offsetof(BlockCounters, executions));
        block->baseline.loads = block_sum(block, offsetof(BlockCounters, loads));
        block->baseline.stores = block_sum(block, offsetof(BlockCounters, stores));
    }
    window_open = TRUE;
}

static void pause_counting(void)
{
    GHashTableIter iterator;
    gpointer value;

    g_hash_table_iter_init(&iterator, blocks);
    while (g_hash_table_iter_next(&iterator, NULL, &value))
    {
        Block *block = value;
        block->counted.executions += block_sum(block, offsetof(BlockCounters, executions)) - block->baseline.executions;
        block->counted.loads += block_sum(block, offsetof(BlockCounters, loads)) - block->baseline.loads;
        block->counted.stores += block_sum(block, offsetof(BlockCounters, stores)) - block->baseline.stores;
    }
    window_open = FALSE;
}

static void write_counts(void)
{
    GHashTableIter iterator;
    gpointer value;
    FILE *output = fopen(output_path, "w");

    if (output == NULL)
    {
        qemu_plugin_outs("instruction_count: cannot open the output file\n");
        return;
    }
    fprintf(output, "# address instructions executions loads stores\n");
    g_hash_table_iter_init(&iterator, blocks);
    while (g_hash_table_iter_next(&iterator, NULL, &value))
    {
        const Block *block = value;

        if (block->counted.executions != 0u)
            fprintf(output, "%" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 "\n", block->address,
                    block->instructions, block->counted.executions, block->counted.loads, block->counted.stores);
    }
    fclose(output);
}

static void close_counting(void)
{
    pause_counting();
    write_counts();
    window_closed = TRUE;
}

static void open_window(unsigned int vcpu_index, void *userdata)
{
    (void) vcpu_index;
    (void) userdata;
    g_mutex_lock(&blocks_lock);
    if (!window_open && !window_closed)
    {
        resume_counting();
        window_just_opened = window_begin == window_end;
    }
    g_mutex_unlock(&blocks_lock);
}

static void close_window(unsigned int vcpu_index, void *userdata)
{
    (void) vcpu_index;
    (void) userdata;
    g_mutex_lock(&blocks_lock);
    if (window_just_opened)
        window_just_opened = FALSE;
    else if (window_open && !window_closed && ++window_end_seen == window_end_executions)
        close_counting();
    g_mutex_unlock(&blocks_lock);
}

static gboolean read_return_address(uint64_t *out)
{
    GByteArray *bytes = g_byte_array_new();
    gboolean read = FALSE;
    uint32_t stack = 0u;
    uint32_t address = 0u;

    if (stack_pointer != NULL && qemu_plugin_read_register(stack_pointer, bytes) == (int) sizeof(stack))
    {
        memcpy(&stack, bytes->data, sizeof(stack));
        g_byte_array_set_size(bytes, 0u);
        if (qemu_plugin_read_memory_vaddr(stack, bytes, sizeof(address)) && bytes->len == sizeof(address))
        {
            memcpy(&address, bytes->data, sizeof(address));
            *out = address;
            read = TRUE;
        }
    }
    g_byte_array_free(bytes, TRUE);
    return read;
}

static void enter_function(unsigned int vcpu_index, void *userdata)
{
    (void) vcpu_index;
    (void) userdata;
    g_mutex_lock(&blocks_lock);
    if (!window_open && !window_closed && !window_skipping && read_return_address(&inside_return))
    {
        ++window_calls;
        if (window_skipped < window_skip)
            window_skipping = TRUE;
        else
            resume_counting();
    }
    g_mutex_unlock(&blocks_lock);
}

static void reach_block(unsigned int vcpu_index, void *userdata)
{
    const Block *block = userdata;

    (void) vcpu_index;
    if (block->address != inside_return)
        return;
    g_mutex_lock(&blocks_lock);
    if (window_skipping)
    {
        window_skipping = FALSE;
        ++window_skipped;
    }
    else if (window_open && !window_closed)
    {
        if (++window_end_seen == window_end_executions)
            close_counting();
        else
            pause_counting();
    }
    g_mutex_unlock(&blocks_lock);
}

static Block *block_for(struct qemu_plugin_tb *translated)
{
    const uint64_t address = qemu_plugin_tb_vaddr(translated);
    Block *block = g_hash_table_lookup(blocks, &address);

    if (block == NULL)
    {
        block = g_new0(Block, 1);
        block->address = address;
        block->instructions = qemu_plugin_tb_n_insns(translated);
        block->counters = qemu_plugin_scoreboard_new(sizeof(BlockCounters));
        g_hash_table_insert(blocks, &block->address, block);
    }
    return block;
}

static void translate(qemu_plugin_id_t id, struct qemu_plugin_tb *translated)
{
    const size_t instructions = qemu_plugin_tb_n_insns(translated);
    Block *block;

    (void) id;
    g_mutex_lock(&blocks_lock);
    block = block_for(translated);
    g_mutex_unlock(&blocks_lock);

    if (window_inside != 0u)
        qemu_plugin_register_vcpu_tb_exec_cb(translated, reach_block, QEMU_PLUGIN_CB_NO_REGS, block);
    qemu_plugin_register_vcpu_tb_exec_inline_per_vcpu(
        translated, QEMU_PLUGIN_INLINE_ADD_U64,
        qemu_plugin_scoreboard_u64_in_struct(block->counters, BlockCounters, executions), 1);
    for (size_t index = 0; index < instructions; ++index)
    {
        struct qemu_plugin_insn *instruction = qemu_plugin_tb_get_insn(translated, index);
        const uint64_t address = qemu_plugin_insn_vaddr(instruction);

        qemu_plugin_register_vcpu_mem_inline_per_vcpu(
            instruction, QEMU_PLUGIN_MEM_R, QEMU_PLUGIN_INLINE_ADD_U64,
            qemu_plugin_scoreboard_u64_in_struct(block->counters, BlockCounters, loads), 1);
        qemu_plugin_register_vcpu_mem_inline_per_vcpu(
            instruction, QEMU_PLUGIN_MEM_W, QEMU_PLUGIN_INLINE_ADD_U64,
            qemu_plugin_scoreboard_u64_in_struct(block->counters, BlockCounters, stores), 1);
        if (window_begin != 0u && address == window_begin)
            qemu_plugin_register_vcpu_insn_exec_cb(instruction, open_window, QEMU_PLUGIN_CB_NO_REGS, NULL);
        if (window_end != 0u && address == window_end)
            qemu_plugin_register_vcpu_insn_exec_cb(instruction, close_window, QEMU_PLUGIN_CB_NO_REGS, NULL);
        if (window_inside != 0u && address == window_inside)
            qemu_plugin_register_vcpu_insn_exec_cb(instruction, enter_function, QEMU_PLUGIN_CB_R_REGS, NULL);
    }
}

static void find_stack_pointer(qemu_plugin_id_t id, unsigned int vcpu_index)
{
    GArray *registers = qemu_plugin_get_registers();

    (void) id;
    (void) vcpu_index;
    for (guint index = 0; index < registers->len; ++index)
    {
        const qemu_plugin_reg_descriptor *descriptor = &g_array_index(registers, qemu_plugin_reg_descriptor, index);

        if (g_strcmp0(descriptor->name, "esp") == 0)
            stack_pointer = descriptor->handle;
    }
    g_array_free(registers, TRUE);
    if (stack_pointer == NULL)
        qemu_plugin_outs("instruction_count: no esp register, so inside= never sees a call begin\n");
}

static void finish(qemu_plugin_id_t id, void *userdata)
{
    (void) id;
    (void) userdata;
    g_mutex_lock(&blocks_lock);
    if (window_begin == 0u && window_inside == 0u && !window_closed)
        close_counting();
    if (window_inside != 0u && !window_closed)
        fprintf(stderr,
                "instruction_count: the window did not close: inside= began %" PRIu64 " calls, skip=%" PRIu64
                " and times=%" PRIu64 " need %" PRIu64 "\n",
                window_calls, window_skip, window_end_executions, window_skip + window_end_executions);
    g_mutex_unlock(&blocks_lock);
}

static gboolean read_address(const char *text, uint64_t *out)
{
    char *end = NULL;

    *out = g_ascii_strtoull(text, &end, 0);
    return end != text && *end == '\0';
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info, int argc, char **argv)
{
    (void) info;
    for (int index = 0; index < argc; ++index)
    {
        char **pair = g_strsplit(argv[index], "=", 2);
        gboolean known = pair[0] != NULL && pair[1] != NULL;

        if (known && g_strcmp0(pair[0], "out") == 0)
            output_path = g_strdup(pair[1]);
        else if (known && g_strcmp0(pair[0], "begin") == 0)
            known = read_address(pair[1], &window_begin);
        else if (known && g_strcmp0(pair[0], "end") == 0)
            known = read_address(pair[1], &window_end);
        else if (known && g_strcmp0(pair[0], "inside") == 0)
            known = read_address(pair[1], &window_inside);
        else if (known && g_strcmp0(pair[0], "times") == 0)
            known = read_address(pair[1], &window_end_executions) && window_end_executions != 0u;
        else if (known && g_strcmp0(pair[0], "skip") == 0)
            known = read_address(pair[1], &window_skip);
        else
            known = FALSE;
        g_strfreev(pair);
        if (!known)
        {
            fprintf(stderr,
                    "instruction_count: unknown argument %s (out=FILE, begin=ADDRESS, end=ADDRESS, inside=ADDRESS, "
                    "times=N, skip=N)\n",
                    argv[index]);
            return -1;
        }
    }
    if (output_path == NULL || (window_begin != 0u) != (window_end != 0u) ||
        (window_inside != 0u && window_begin != 0u) || (window_skip != 0u && window_inside == 0u))
    {
        fprintf(stderr,
                "instruction_count: needs out=FILE, and begin= with end=, or inside= with skip= if any, or neither\n");
        return -1;
    }

    blocks = g_hash_table_new(g_int64_hash, g_int64_equal);
    if (window_inside != 0u)
        qemu_plugin_register_vcpu_init_cb(id, find_stack_pointer);
    qemu_plugin_register_vcpu_tb_trans_cb(id, translate);
    qemu_plugin_register_atexit_cb(id, finish, NULL);
    return 0;
}
