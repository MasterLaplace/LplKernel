/**
 * @file telemetry.h
 * @brief One record, one schema, one reader — so a grep cannot match the wrong field.
 *
 * The kernel already counts everything worth counting: real-time violations, dropped
 * bytes, duty cycle, page counts, timeouts. It printed them as prose on COM1 and
 * validate.sh read them back with grep, and that arrangement has produced a false
 * verdict twice. Once because `refusals=` also matches `world_refusals=` on the same
 * line, so the extracted value held two numbers and the comparison became a NaN. Once
 * because two gates named a field `steps=` and the second definition silently
 * overwrote the first, making the kernel disagree with an oracle that was itself wrong.
 *
 * Neither was a careless grep. Both are what an unanchored namespace does eventually.
 * A record here is:
 *
 *     [LPLTLM] <domain> <key>=<value> <key>=<value> ...
 *
 * The domain sits on the same line as every one of its fields, at a fixed position, so
 * a reader anchored on "[LPLTLM] <domain> " followed by " <key>=" cannot reach into
 * another domain. Two subsystems may both publish `refusals` and never collide.
 *
 * Two properties are enforced rather than hoped for, and both are counted so a check
 * can fail on them: a key repeated inside one record, and a value carrying a space or
 * an '=' — either would make the line ambiguous to its own reader.
 *
 * Scope, stated so it is not mistaken for an omission: this does NOT retrofit the
 * existing prose. Rewriting every log line would churn dozens of greps in validate.sh
 * for no measurement gained. New records go through here; the old lines stay as they
 * are until something needs them.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#ifndef KERNEL_DIAG_TELEMETRY_H
#define KERNEL_DIAG_TELEMETRY_H

#include <stdbool.h>
#include <stdint.h>

#include <kernel/drivers/serial.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Line marker. Distinct from the "[LplKernel]:" prose prefix on purpose. */
#define KERNEL_TELEMETRY_RECORD_PREFIX "[LPLTLM]"

/** Fields one record may carry. Past this the field is dropped and counted. */
#define KERNEL_TELEMETRY_MAX_FIELDS_PER_RECORD 32u

/**
 * @brief Open a record for one domain.
 *
 * @param serial Output port; the record is written as a single line.
 * @param domain Subsystem name, the namespace every field of this record hangs off.
 *               Must be free of spaces and '=' for the same reason values are.
 */
void kernel_telemetry_begin_record(Serial_t *serial, const char *domain);

/**
 * @brief Append a decimal field to the open record.
 *
 * @param key Field name, unique within the record.
 * @param value Value written in base ten.
 */
void kernel_telemetry_write_unsigned(const char *key, uint32_t value);

/**
 * @brief Append a hexadecimal field to the open record.
 *
 * @param key Field name, unique within the record.
 * @param value Value written as 0x followed by eight digits.
 */
void kernel_telemetry_write_hexadecimal(const char *key, uint32_t value);

/**
 * @brief Append a boolean field to the open record, as 0 or 1.
 *
 * @param key Field name, unique within the record.
 * @param value Value.
 */
void kernel_telemetry_write_boolean(const char *key, bool value);

/**
 * @brief Append a textual field to the open record.
 *
 * A space or an '=' inside the value would split the field in two for any reader,
 * so each is replaced by '_' and counted.
 *
 * @param key Field name, unique within the record.
 * @param value Value.
 */
void kernel_telemetry_write_text(const char *key, const char *value);

/**
 * @brief Close the open record and terminate its line.
 */
void kernel_telemetry_end_record(void);

/**
 * @brief Records emitted since boot.
 *
 * @return The count.
 */
uint32_t kernel_telemetry_get_record_count(void);

/**
 * @brief Times a key was written twice inside one record.
 *
 * @details A duplicate makes the record ambiguous to its own reader, which is the
 *          defect this module exists to make impossible. Expected to stay zero.
 *
 * @return The count.
 */
uint32_t kernel_telemetry_get_duplicate_key_count(void);

/**
 * @brief Characters replaced because a value carried a space or an '='.
 *
 * @return The count. Expected to stay zero.
 */
uint32_t kernel_telemetry_get_sanitised_character_count(void);

/**
 * @brief Fields dropped because a record exceeded KERNEL_TELEMETRY_MAX_FIELDS_PER_RECORD.
 *
 * @return The count. Expected to stay zero.
 */
uint32_t kernel_telemetry_get_dropped_field_count(void);

/**
 * @brief Emit the telemetry module's own counters as a record.
 *
 * The three hygiene counters are expected to read zero, and a reader that checks
 * them is checking that every OTHER record on the line is unambiguous. `records`
 * counts what was emitted BEFORE this one, since a record is only counted once it
 * is closed.
 *
 * @param serial Output port.
 */
void kernel_telemetry_report(Serial_t *serial);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_DIAG_TELEMETRY_H */
