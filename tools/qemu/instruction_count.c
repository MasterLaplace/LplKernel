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
    BlockCounters baseline;                  /**< The counters when the window opened, summed. */
} Block;

static GMutex blocks_lock;
static GHashTable *blocks;
static char *output_path;
static uint64_t window_begin;
static uint64_t window_end;
static gboolean window_open;
static gboolean window_closed;

static uint64_t block_sum(const Block *block, size_t offset)
{
    return qemu_plugin_u64_sum((qemu_plugin_u64){block->counters, offset});
}

static void open_window(unsigned int vcpu_index, void *userdata)
{
    GHashTableIter iterator;
    gpointer value;

    (void) vcpu_index;
    (void) userdata;
    g_mutex_lock(&blocks_lock);
    if (!window_open && !window_closed)
    {
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
    g_mutex_unlock(&blocks_lock);
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
        const uint64_t executions = block_sum(block, offsetof(BlockCounters, executions)) - block->baseline.executions;
        const uint64_t loads = block_sum(block, offsetof(BlockCounters, loads)) - block->baseline.loads;
        const uint64_t stores = block_sum(block, offsetof(BlockCounters, stores)) - block->baseline.stores;

        if (executions != 0u)
            fprintf(output, "%" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 "\n", block->address,
                    block->instructions, executions, loads, stores);
    }
    fclose(output);
}

static void close_window(unsigned int vcpu_index, void *userdata)
{
    (void) vcpu_index;
    (void) userdata;
    g_mutex_lock(&blocks_lock);
    if (window_open && !window_closed)
    {
        write_counts();
        window_closed = TRUE;
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
    }
}

static void finish(qemu_plugin_id_t id, void *userdata)
{
    (void) id;
    (void) userdata;
    g_mutex_lock(&blocks_lock);
    if (window_begin == 0u && !window_closed)
    {
        write_counts();
        window_closed = TRUE;
    }
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
        else
            known = FALSE;
        g_strfreev(pair);
        if (!known)
        {
            fprintf(stderr, "instruction_count: unknown argument %s (out=FILE, begin=ADDRESS, end=ADDRESS)\n",
                    argv[index]);
            return -1;
        }
    }
    if (output_path == NULL || (window_begin != 0u) != (window_end != 0u))
    {
        fprintf(stderr, "instruction_count: needs out=FILE, and begin= and end= together or neither\n");
        return -1;
    }

    blocks = g_hash_table_new(g_int64_hash, g_int64_equal);
    qemu_plugin_register_vcpu_tb_trans_cb(id, translate);
    qemu_plugin_register_atexit_cb(id, finish, NULL);
    return 0;
}
