/**************************************************************************
 * LplKernel - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under the MIT License.
 * https://opensource.org/license/mit
 * Copyright © 2026 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is free software: you can use, copy, modify, merge, publish and
 * distribute it under the terms of the MIT License, provided this copyright
 * notice and the permission notice are kept. See the LICENSE file.
 *
 * @file machine_language_namespace.h
 * @brief The ACPI namespace: a node arena and one hash keyed by parent and NameSeg.
 *
 * Layer L2 of the specification (docs/acpi/specification.md, §3.4 and §5.1). Nodes are 40 bytes,
 * hold no pointer, and link to each other by index, index 0 meaning none; they live in runs of
 * pages the host gives. A lookup is one probe sequence in an open-addressing table, never a scan
 * of siblings.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-10-24
 **************************************************************************/

#ifndef KERNEL_ACPI_MACHINE_LANGUAGE_NAMESPACE_H
#define KERNEL_ACPI_MACHINE_LANGUAGE_NAMESPACE_H

#include <kernel/acpi/host.h>
#include <kernel/acpi/machine_language_decoder.h>

#include <stdbool.h>
#include <stdint.h>

#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_PAGE_BYTES 4096u /**< Bytes of one host page. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RUN_PAGES                                          \
    16u /**< Pages of one run of nodes: 64 KiB. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODES_PER_RUN                                      \
    (ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RUN_PAGES *                                           \
     ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_PAGE_BYTES /                                          \
     (uint32_t) sizeof(AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t)) /**< Nodes one run holds. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SERVER_MAX_NODES                                   \
    65536u /**< Namespace nodes on the server profile (§6.1). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OTHER_MAX_NODES                                    \
    32768u /**< Namespace nodes on the other profiles (§6.1). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SERVER_MAX_ARENA_BYTES                             \
    (8u * 1024u * 1024u) /**< Namespace arena on the server profile (§6.1). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OTHER_MAX_ARENA_BYTES                              \
    (4u * 1024u * 1024u) /**< Namespace arena on the other profiles (§6.1). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_RUNS                                           \
    41u /**< Runs of nodes: enough for the server cap and the unused node 0. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT 1u /**< The root's node index. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_PROCESSOR_IDENTIFIER_SHIFT                         \
    40u /**< Where a Processor node's payload keeps its ProcessorID, above its PblkAddr (bits 0 to 31) and its PblkLen \
             (bits 32 to 39). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_FREE                                          \
    0x01u /**< Node flag: the node was removed, and its index waits on the free list. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_HOLDS_INITIALIZATION                          \
    0x02u /**< Node flag: the node, or one below it, is the device, processor or thermal zone of an _INI, as the       \
               initialisation's pre-pass marks it (§3.8 step 4). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_HOLDS_CAPABILITIES                            \
    0x04u /**< Node flag: the node, or one below it, is a device or processor with an _OSC or a _PDC, as the           \
               initialisation's pre-pass marks it (§3.8 step 7). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_ALIAS_HOPS                                     \
    8u /**< Aliases one resolution follows before it gives up. */

/**
 * @brief What a node is: the ACPI ObjectType numbers (ACPI 6.6 §19.6.97), then the kinds of node
 *        that are not objects.
 */
typedef enum AdvancedConfigurationAndPowerInterfaceMachineLanguageNodeType {
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_UNINITIALIZED =
        0,                                                                           /**< A Name not evaluated yet. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_INTEGER = 1,    /**< An Integer. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_STRING = 2,     /**< A String. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_BUFFER = 3,     /**< A Buffer. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_PACKAGE = 4,    /**< A Package. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_FIELD_UNIT = 5, /**< A unit of a field. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_DEVICE = 6,     /**< A Device. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_EVENT = 7,      /**< An Event. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_METHOD = 8,     /**< A control method. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_MUTEX = 9,      /**< A Mutex. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_OPERATION_REGION = 10, /**< A region, data table
                                                                                                 regions included. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_POWER_RESOURCE = 11,   /**< A PowerResource. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_PROCESSOR = 12,        /**< A Processor. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_THERMAL_ZONE = 13,     /**< A ThermalZone. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_BUFFER_FIELD = 14,     /**< A buffer field. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_SCOPE = 0x80,          /**< A predefined scope. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_ALIAS = 0x81,    /**< An Alias: value.handle is its
                                                                                          target. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_EXTERNAL = 0x82, /**< An External: value.auxiliary
                                                                                          is its ObjectType and
                                                                                          value.payload its argument
                                                                                          count. Not an object: only
                                                                                          the decoder reads it. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageNodeType_t;

/**
 * @brief A value: an Integer in its payload, or a handle on what holds it (§3.4).
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguageValue {
    uint8_t kind;       /**< What the value is. */
    uint8_t flags;      /**< Bits the kind defines. */
    uint16_t auxiliary; /**< A small field the kind defines: a method's MethodFlags. */
    uint32_t handle;    /**< A node or object index, or the table a method's body lies in. */
    uint64_t payload;   /**< An Integer, or a method's body offset (low half) and length (high half). */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t;

/**
 * @brief One node: a name in its parent, its links, its type, and its value (§3.4).
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguageNode {
    uint32_t name;         /**< Its NameSeg, as advanced_configuration_and_power_interface_machine_language_name_segment
                                gives it. */
    uint32_t parent;       /**< Its parent's index; 0 for the root. */
    uint32_t first_child;  /**< Its first child in declaration order, or 0. */
    uint32_t next_sibling; /**< The sibling declared after it, or 0. */
    uint32_t previous_sibling; /**< The sibling declared before it; the first child's is the last child. */
    uint8_t type;              /**< An ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_* value. */
    uint8_t flags;             /**< ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_* flags. */
    uint16_t owner;            /**< The table that declared it, counted from 1; 0 for a predefined node. */
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t value; /**< Its object, when it fits in a value. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t;

/**
 * @brief A namespace: its runs of nodes, its hash table, its caps and what its lookups cost.
 *
 * @details Opened by advanced_configuration_and_power_interface_machine_language_namespace_open,
 *          which creates the root and the predefined objects; every page it holds comes from its
 *          host and goes back with advanced_configuration_and_power_interface_machine_language_namespace_close.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace {
    const AdvancedConfigurationAndPowerInterfaceHost_t *host; /**< Where its pages come from. */
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t
        *runs[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_RUNS]; /**< Its runs of nodes. */
    uint32_t *slots;          /**< The hash table: a node index per slot, 0 when empty. */
    uint32_t slot_capacity;   /**< Slots, a power of two at least twice the nodes. */
    uint32_t run_count;       /**< Runs taken. */
    uint32_t node_count;      /**< Node indices used, the unused node 0 included. */
    uint32_t free_nodes;      /**< The last node removed, whose next_sibling links the others, or 0. */
    uint32_t live_nodes;      /**< Nodes in the namespace, the root included, the removed ones apart. */
    uint32_t max_nodes;       /**< The node cap. */
    uint32_t arena_bytes;     /**< Bytes taken from the host: runs and hash table. */
    uint32_t max_arena_bytes; /**< The arena cap. */
    uint32_t lookups;         /**< Lookups of a child by name. */
    uint32_t probes;          /**< Slots those lookups read. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t;

/**
 * @brief Opens an empty namespace, then creates the root and the predefined objects: the scopes
 *        `\_GPE`, `\_PR_` and `\_SI_`, the devices `\_SB_` and `\_TZ_`, the method `\_OSI` of one
 *        argument, the mutex `\_GL_`, and the names `\_OS_` and `\_REV`.
 *
 * @param space           Filled; closed with
 * advanced_configuration_and_power_interface_machine_language_namespace_close, even when this fails.
 * @param host            The host whose allocate_pages and release_pages it uses.
 * @param max_nodes       The node cap.
 * @param max_arena_bytes The arena cap.
 * @return OUT_OF_MEMORY when the host gives no pages or the caps cannot hold the predefined
 *         objects; OK otherwise.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_namespace_open(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space,
    const AdvancedConfigurationAndPowerInterfaceHost_t *host, uint32_t max_nodes, uint32_t max_arena_bytes);

/**
 * @brief Takes pages from the namespace's host for the arena, within its arena cap: the namespace's
 *        own runs and hash table, and the objects its nodes hold.
 *
 * @param space The namespace.
 * @param count Pages of 4 KiB.
 * @return The pages, or NULL when the host gives none or the arena cap would be passed.
 */
extern void *advanced_configuration_and_power_interface_machine_language_namespace_take_pages(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint32_t count);

/**
 * @brief Gives back pages advanced_configuration_and_power_interface_machine_language_namespace_take_pages took.
 *
 * @param space The namespace.
 * @param pages The pages.
 * @param count How many were taken.
 */
extern void advanced_configuration_and_power_interface_machine_language_namespace_give_pages(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, void *pages, uint32_t count);

/**
 * @brief Gives every page of a namespace back to its host, and empties it.
 *
 * @param space The namespace; its nodes must not be used afterwards.
 */
extern void advanced_configuration_and_power_interface_machine_language_namespace_close(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space);

/**
 * @brief The node at an index.
 *
 * @param space The namespace.
 * @param index A node index.
 * @return The node, or NULL for 0 and indices not used yet.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *
advanced_configuration_and_power_interface_machine_language_namespace_node(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint32_t index);

/**
 * @brief Finds the child of @p parent named @p name.
 *
 * @param space  The namespace.
 * @param parent The parent's index.
 * @param name   The NameSeg, as an integer.
 * @return The child's index, or 0 when @p parent has no such child.
 */
extern uint32_t advanced_configuration_and_power_interface_machine_language_namespace_find_child(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint32_t parent, uint32_t name);

/**
 * @brief Finds the object the child of @p parent named @p name holds: an alias is followed to what
 *        it names.
 *
 * @param space  The namespace.
 * @param parent The parent's index.
 * @param name   The NameSeg, as an integer.
 * @return The index of the node that holds the object, or 0 when @p parent has no such child, when it
 *         is an External that no table declared, or when its aliases do not end within
 *         ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_ALIAS_HOPS.
 */
extern uint32_t advanced_configuration_and_power_interface_machine_language_namespace_find_object(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint32_t parent, uint32_t name);

/**
 * @brief The node after @p index in a depth-first walk in declaration order, children after their
 *        parent.
 *
 * @param space   The namespace.
 * @param index   The node the walk stands on.
 * @param descend Whether the walk goes into @p index's children, or skips them.
 * @return The next node, or 0 once the walk is back at the root.
 */
extern uint32_t advanced_configuration_and_power_interface_machine_language_namespace_next(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint32_t index, bool descend);

/**
 * @brief Writes the absolute path of a node, such as `\_SB_.PCI0._STA`, its NameSegs four characters
 *        each, ended by a NUL.
 *
 * @param space    The namespace.
 * @param index    The node.
 * @param out      Receives the path.
 * @param capacity The bytes @p out holds.
 * @return The path's length without its NUL; 0, @p out untouched, when it does not fit.
 */
extern uint32_t advanced_configuration_and_power_interface_machine_language_namespace_path(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint32_t index, char *out,
    uint32_t capacity);

/**
 * @brief Creates a child of @p parent, last in declaration order.
 *
 * @param space     The namespace.
 * @param parent    The parent's index; it must have no child named @p name.
 * @param name      The NameSeg, as an integer.
 * @param type      An ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_* value.
 * @param owner     The table that declares it, counted from 1, or 0.
 * @details It takes the index of the node removed last, when there is one.
 * @param out_index Receives the new node's index.
 * @return LIMIT_NAMESPACE_NODES when the node cap is reached; OUT_OF_MEMORY when the host gives no
 *         pages or the arena cap is reached; OK otherwise.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_namespace_add_child(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint32_t parent, uint32_t name,
    uint8_t type, uint16_t owner, uint32_t *out_index);

/**
 * @brief Removes a node that has no child: it leaves its parent's children and the hash table, by
 *        backward shifting so that no tombstone is left, and its index goes on the free list.
 *
 * @param space The namespace.
 * @param index The node's index; it must have no child.
 */
extern void advanced_configuration_and_power_interface_machine_language_namespace_remove(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint32_t index);

/**
 * @brief Finds the node a NameString refers to from a scope (ACPI 6.6 §5.3): only a single NameSeg
 *        without prefix searches the scope, then each parent up to the root; aliases are followed.
 *
 * @param space     The namespace.
 * @param scope     The index of the scope the name is read in.
 * @param bytes     The AML the NameString was read from.
 * @param name      The NameString.
 * @param out_index Receives the node's index.
 * @return NAME_NOT_FOUND when no node has that name, when the name climbs above the root, or when
 *         an alias leads nowhere within ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_ALIAS_HOPS;
 *         OK otherwise.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_namespace_resolve(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint32_t scope, const uint8_t *bytes,
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNameString_t *name, uint32_t *out_index);

/**
 * @brief Finds where a declaration of a NameString in a scope creates its node: every segment but
 *        the last must exist, and only the last is created.
 *
 * @param space      The namespace.
 * @param scope      The index of the scope the declaration is read in.
 * @param bytes      The AML the NameString was read from.
 * @param name       The NameString; NullName declares nothing.
 * @param out_parent Receives the index of the node the last segment belongs to.
 * @param out_name   Receives the last segment.
 * @return NAME_NOT_FOUND when the name is NullName, climbs above the root, or crosses a segment
 *         that does not exist; OK otherwise.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_namespace_creation_place(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint32_t scope, const uint8_t *bytes,
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNameString_t *name, uint32_t *out_parent,
    uint32_t *out_name);

#endif /* KERNEL_ACPI_MACHINE_LANGUAGE_NAMESPACE_H */
