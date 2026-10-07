#define __LPL_KERNEL__
#include <kernel/config.h>

#include <kernel/testing/smoke_libengine.h>

#if defined(LPL_KERNEL_ENABLE_SMOKE_TESTS) &&                                                                          \
    (!defined(LPL_ASSISTANT_UNAVAILABLE) || !defined(LPL_KNOWLEDGE_UNAVAILABLE))

#    include <kernel/cpu/pic.h>
#    include <kernel/diag/telemetry.h>
#    include <kernel/drivers/hda.h>
#    include <kernel/hal/hal_audio.h>
#    include <kernel/power/frequency_scaling.h>
#    include <kernel/power/processor_sleep.h>
#    include <kernel/power/wakeup_accounting.h>

#    if !defined(LPL_ASSISTANT_UNAVAILABLE)
#        include <kernel/ai/inference_budget.h>
#        include <kernel/ai/model_slot.h>
#        include <kernel/ai/tensor_arena.h>
#        include <kernel/dialogue/dialogue_channel.h>
#        include <kernel/drivers/hda.h>
#        include <kernel/satellite/satellite_app.h>
#        include <libassistant/libassistant.h>
#    endif

#    if !defined(LPL_KNOWLEDGE_UNAVAILABLE)
#        include <libknowledge/libknowledge.h>
#    endif

/** One labelled value of a smoke report line. */
typedef struct {
    const char *label;
    uint32_t value;
} SmokeLibengineRow_t;

/** Element count of a row table whose size is known at compile time. */
#    define SMOKE_LIBENGINE_ROW_COUNT(rows) (sizeof(rows) / sizeof((rows)[0]))

/**
 * @brief Writes @p prefix, then every row as its label followed by its value in hexadecimal.
 *
 * @details Leaves the line open, so a caller can append fields that are not numbers.
 *
 * @param com1   Serial port the line is written to.
 * @param prefix Text that opens the line.
 * @param rows   The rows.
 * @param count  How many rows there are.
 */
static void smoke_libengine_write_rows(Serial_t *com1, const char *prefix, const SmokeLibengineRow_t *rows,
                                       size_t count)
{
    serial_write_string(com1, prefix);
    for (size_t i = 0u; i < count; ++i)
    {
        serial_write_string(com1, rows[i].label);
        serial_write_hex32(com1, rows[i].value);
    }
}

#    if !defined(LPL_ASSISTANT_UNAVAILABLE)

/**
 * @brief Writes a whole report line: @p prefix, every row, then the end of the line.
 *
 * @param com1   Serial port the line is written to.
 * @param prefix Text that opens the line.
 * @param rows   The rows.
 * @param count  How many rows there are.
 */
static void smoke_libengine_write_line(Serial_t *com1, const char *prefix, const SmokeLibengineRow_t *rows,
                                       size_t count)
{
    smoke_libengine_write_rows(com1, prefix, rows, count);
    serial_write_string(com1, "\n");
}

/**
 * @brief Boots the assistant and says whether the mind came up, with its slot and arena.
 *
 * @param com1 Serial port the report is written to.
 * @return true when the mind is ready.
 */
static bool smoke_libengine_report_assistant_boot(Serial_t *com1)
{
    const bool up = libassistant_boot(libassistant_recommended_arena_bytes());
    serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: libassistant boot: ");
    serial_write_string(com1, up ? "ready" : "FAILED");
    serial_write_string(com1, ", model slot=");
    serial_write_string(com1, libassistant_model_slot_state());
    serial_write_string(com1, ", arena=");
    serial_write_hex32(com1, (uint32_t) kernel_tensor_arena_size());
    serial_write_string(com1, "\n");
    return up;
}

/**
 * @brief Puts one question through the dialogue aperture and reports the round trip.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_dialogue_round_trip(Serial_t *com1)
{
    libassistant_dialogue_result_t dialogue;
    const bool answered = libassistant_dialogue_round_trip(&dialogue);
    const SmokeLibengineRow_t dialogue_rows[] = {
        {"question=",     dialogue.question_bytes},
        {", consumed=",   dialogue.consumed      },
        {", answer=",     dialogue.answer_bytes  },
        {", delivered=",  dialogue.delivered     },
        {", dropped=",    dialogue.dropped       },
        {", spent=",      dialogue.budget_spent  },
        {", denied=",     dialogue.budget_denied },
        {", answer_sig=", dialogue.answer_sig    },
        {", valid=",      dialogue.valid_call    },
    };
    smoke_libengine_write_rows(com1, "[" KERNEL_SYSTEM_STRING "]: libassistant dialogue: ", dialogue_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(dialogue_rows));
    serial_write_string(com1, answered ? " (ok)\n" : " (no valid call)\n");
}

/**
 * @brief Gate P14: the demon thinks in ring 0, and thinks the same thing the host does.
 *
 * @details Everything is integer — eight-bit weights, Q16.16 activations, an exponential built
 *          from a shift and six Taylor terms, rotary angles from CORDIC — because a float
 *          forward pass would put the answer one rounding mode away from a different one, and
 *          every gate downstream of an answer assumes the answer is reproducible. Must match
 *          LplAssistant/tests/test_infer_parity.cpp.
 *
 * @note The aperture is exercised first and the gate second, in that order and not the other
 *       way round: the gate re-carves the tensor arena from zero, so the live mind does not
 *       survive it.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p14_mind(Serial_t *com1)
{
    if (smoke_libengine_report_assistant_boot(com1))
        smoke_libengine_report_dialogue_round_trip(com1);

    libassistant_mind_fold_result_t mind;
    libassistant_mind_fold(&mind);
    const SmokeLibengineRow_t mind_rows[] = {
        {"weight_sig=",        mind.weight_sig        },
        {", prompt_sig=",      mind.prompt_sig        },
        {", logit_sig=",       mind.logit_sig         },
        {", residual_sig=",    mind.residual_sig      },
        {", token_sig=",       mind.token_sig         },
        {", constrained_sig=", mind.constrained_sig   },
        {", text_sig=",        mind.text_sig          },
        {", vocab=",           mind.vocab             },
        {", prompt_tokens=",   mind.prompt_tokens     },
        {", generated=",       mind.generated         },
        {", draws=",           mind.draws             },
        {", constrained=",     mind.constrained_tokens},
        {", admitted=",        mind.admitted_first    },
        {", grammar_done=",    mind.grammar_complete  },
        {", forbidden=",       mind.forbidden         },
        {", blob_bytes=",      mind.blob_bytes        },
        {", blob_reopened=",   mind.blob_reopened     },
        {", arena_bytes=",     mind.arena_bytes       },
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: libassistant P14 mind: ", mind_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(mind_rows));
}

/**
 * @brief The satellite run's counters on one line, followed by the codec it found.
 *
 * @param com1      Serial port the report is written to.
 * @param satellite What the run measured.
 */
static void smoke_libengine_report_power_counters(Serial_t *com1, const SatelliteReport_t *satellite)
{
    const SmokeLibengineRow_t power_rows[] = {
        {"iterations=", satellite->idle_iterations  },
        {", sleeps=",   satellite->sleeps           },
        {", skipped=",  satellite->sleeps_skipped   },
        {", halts=",    satellite->halts            },
        {", duty=",     satellite->duty_permille    },
        {", avoided=",  satellite->ticks_avoided    },
        {", monitor=",  satellite->monitor_available},
        {", scaling=",  satellite->scaling_available},
        {", refused=",  satellite->scaling_refused  },
        {", state=",    satellite->governed_state   },
        {", audio=",    satellite->audio_present    },
        {", ceiling=",  satellite->output_ceiling   },
        {", clipped=",  satellite->limiter_clipped  },
        {", peak=",     satellite->limiter_peak     },
        {", captured=", satellite->frames_captured  },
        {", level=",    satellite->capture_peak     },
    };
    smoke_libengine_write_rows(com1, "[" KERNEL_SYSTEM_STRING "]: power floor: ", power_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(power_rows));
    serial_write_string(com1, ", codec=");
    serial_write_string(com1, kernel_satellite_app_audio_name());
    serial_write_string(com1, "\n");
}

/**
 * @brief The sleep depth the processor offers, the one in force, and how often it was lowered.
 *
 * @param com1 Serial port the record is written to.
 */
static void smoke_libengine_report_sleep_depth(Serial_t *com1)
{
    kernel_telemetry_begin_record(com1, "sleep_depth_live");
    kernel_telemetry_write_unsigned("available", kernel_processor_sleep_available_hints());
    kernel_telemetry_write_unsigned("active", kernel_processor_sleep_active_hint());
    kernel_telemetry_write_unsigned("clamped", kernel_processor_sleep_clamped_count());
    kernel_telemetry_write_boolean("interrupt_break", kernel_processor_sleep_has_interrupt_break());
    kernel_telemetry_end_record();
}

/**
 * @brief Appends the capture stream's registers, read at this instant, to the open record.
 * @param line Legacy line the capture interrupt was routed on.
 */
static void smoke_libengine_write_capture_registers(uint8_t line)
{
    uint8_t stream_status = 0u;
    uint32_t interrupt_status = 0u;
    uint32_t position = 0u;
    intel_high_definition_audio_capture_registers(&stream_status, &interrupt_status, &position);

    kernel_telemetry_write_hexadecimal("stream_status", stream_status);
    kernel_telemetry_write_hexadecimal("interrupt_status", interrupt_status);
    kernel_telemetry_write_unsigned("position", position);
    kernel_telemetry_write_unsigned("pic_in_service", programmable_interrupt_controller_is_in_service(line));
}

/**
 * @brief How capture was driven and what the stream's registers say about it.
 *
 * @param com1      Serial port the record is written to.
 * @param satellite What the run measured.
 */
static void smoke_libengine_report_audio_capture(Serial_t *com1, const SatelliteReport_t *satellite)
{
    const uint8_t line = hardware_abstraction_layer_audio_capture_interrupt_line();

    kernel_telemetry_begin_record(com1, "audio_capture");
    kernel_telemetry_write_boolean("interrupt_driven", hardware_abstraction_layer_audio_capture_is_interrupt_driven());
    kernel_telemetry_write_unsigned("line", line);
    kernel_telemetry_write_unsigned("interrupts", hardware_abstraction_layer_audio_capture_interrupt_count());
    kernel_telemetry_write_unsigned("frames", satellite->frames_captured);
    kernel_telemetry_write_unsigned("overruns", hardware_abstraction_layer_audio_capture_overruns());
    smoke_libengine_write_capture_registers(line);
    kernel_telemetry_end_record();
}

/**
 * @brief The clock the processor actually delivered against the state that was requested.
 *
 * @param com1      Serial port the record is written to.
 * @param satellite What the run measured.
 */
static void smoke_libengine_report_frequency_feedback(Serial_t *com1, const SatelliteReport_t *satellite)
{
    kernel_telemetry_begin_record(com1, "frequency_feedback_live");
    kernel_telemetry_write_boolean("available", kernel_frequency_scaling_feedback_available());
    kernel_telemetry_write_unsigned("effective_permille", satellite->effective_permille);
    kernel_telemetry_write_unsigned("requested_state", satellite->governed_state);
    kernel_telemetry_end_record();
}

/**
 * @brief The audio controller, in its own words.
 *
 * @details Reported field by field because the interesting failures are partial: a controller
 *          that resets but whose codec never announces itself, and one whose codec answers,
 *          are different problems that a single boolean cannot tell apart.
 *
 * @param com1 Serial port the report is written to.
 * @param hda  The controller's bring-up state.
 */
static void smoke_libengine_report_intel_hda(Serial_t *com1, const IntelHighDefinitionAudioState_t *hda)
{
    const SmokeLibengineRow_t hda_rows[] = {
        {"present=",     (uint32_t) hda->controller_present},
        {", running=",   (uint32_t) hda->controller_running},
        {", rings=",     (uint32_t) hda->rings_running     },
        {", version=",   (uint32_t) hda->major_version     },
        {", in=",        (uint32_t) hda->input_streams     },
        {", out=",       (uint32_t) hda->output_streams    },
        {", codecs=",    (uint32_t) hda->codec_mask        },
        {", vendor0=",   hda->codec_vendor[0]              },
        {", verbs=",     hda->verbs_sent                   },
        {", answers=",   hda->responses_read               },
        {", timeouts=",  hda->verb_timeouts                },
        {", widgets=",   (uint32_t) hda->widgets_walked    },
        {", converter=", (uint32_t) hda->capture_converter },
        {", pin=",       (uint32_t) hda->capture_pin       },
        {", playpin=",   (uint32_t) hda->playback_pin      },
        {", muted=",     (uint32_t) hda->outputs_muted     },
        {", capturing=", (uint32_t) hda->capture_running   },
        {", position=",  hda->capture_position             },
        {", wraps=",     hda->capture_wraps                },
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: intel-hda: ", hda_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(hda_rows));
}

/**
 * @brief The command rings either side of the first command that went unanswered.
 *
 * @details This exists because reasoning about this path produced two confident wrong answers:
 *          the registers say whether the controller ever fetched the entry, which no amount of
 *          reading the driver could settle.
 *
 * @param com1 Serial port the report is written to.
 * @param hda  The controller's bring-up state; nothing is written unless it captured a probe.
 */
static void smoke_libengine_report_intel_hda_probe(Serial_t *com1, const IntelHighDefinitionAudioState_t *hda)
{
    if (!hda->probe_captured)
        return;

    const SmokeLibengineRow_t probe_rows[] = {
        {"command=",      hda->probe_command                                       },
        {", corbwp_pre=", (uint32_t) hda->probe_before.command_write_pointer       },
        {", corbrp_pre=", (uint32_t) hda->probe_before.command_read_pointer        },
        {", rirbwp_pre=", (uint32_t) hda->probe_before.response_write_pointer      },
        {", rd_pre=",     (uint32_t) hda->probe_before.response_read_pointer_shadow},
        {", corbwp=",     (uint32_t) hda->probe_after.command_write_pointer        },
        {", shadow=",     (uint32_t) hda->probe_after.command_write_pointer_shadow },
        {", corbrp=",     (uint32_t) hda->probe_after.command_read_pointer         },
        {", rirbwp=",     (uint32_t) hda->probe_after.response_write_pointer       },
        {", rd=",         (uint32_t) hda->probe_after.response_read_pointer_shadow },
        {", corbctl=",    (uint32_t) hda->probe_after.command_ring_control         },
        {", corbsts=",    (uint32_t) hda->probe_after.command_ring_status          },
        {", rirbctl=",    (uint32_t) hda->probe_after.response_ring_control        },
        {", rirbsts=",    (uint32_t) hda->probe_after.response_ring_status         },
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: intel-hda probe: ", probe_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(probe_rows));
}

/**
 * @brief The power floor, measured rather than promised.
 *
 * @details A profile whose whole purpose is to spend nothing has to report a number, or "it
 *          idles cheaply" is indistinguishable from a spin loop. What is printed is what the
 *          hardware actually did: whether it has MONITOR/MWAIT at all, how many sleeps it
 *          really took, and how much of the accounted time it was awake.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_power_floor(Serial_t *com1)
{
    SatelliteReport_t satellite;
    if (!kernel_satellite_app_run(8u, &satellite))
        return;

    smoke_libengine_report_power_counters(com1, &satellite);
    kernel_wakeup_accounting_report(com1);
    smoke_libengine_report_sleep_depth(com1);
    smoke_libengine_report_audio_capture(com1, &satellite);
    smoke_libengine_report_frequency_feedback(com1, &satellite);

    const IntelHighDefinitionAudioState_t *const hda = intel_high_definition_audio_state();
    smoke_libengine_report_intel_hda(com1, hda);
    smoke_libengine_report_intel_hda_probe(com1, hda);
}

/**
 * @brief Gate P15: one protocol, three machines that share no instruction set, one set of
 *        decisions about the same audio.
 *
 * @details The audio is synthesised from the frame index alone so no recording has to reach
 *          both sides — the same reason the world gate derives a world from a seed. Must match
 *          LplAssistant/tests/test_satellite_parity.cpp.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p15_satellite(Serial_t *com1)
{
    libassistant_satellite_fold_result_t node;
    libassistant_satellite_fold(&node);
    const SmokeLibengineRow_t node_rows[] = {
        {"feature_sig=",    node.feature_sig    },
        {", level_sig=",    node.level_sig      },
        {", event_sig=",    node.event_sig      },
        {", wire_sig=",     node.wire_sig       },
        {", state_sig=",    node.state_sig      },
        {", template_sig=", node.template_sig   },
        {", emitted=",      node.emitted        },
        {", utterances=",   node.utterances     },
        {", detections=",   node.detections     },
        {", wake_frame=",   node.wake_frame     },
        {", wake_dist=",    node.wake_distance  },
        {", speech_dist=",  node.speech_distance},
        {", echoes=",       node.echoes         },
        {", transitions=",  node.transitions    },
        {", idle=",         node.idle_permille  },
        {", duty=",         node.duty_permille  },
        {", tagged_audio=", node.tagged_audio   },
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: libassistant P15 satellite: ", node_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(node_rows));
}

/**
 * @brief Gate P16: one turn of thought, decided identically on both targets.
 *
 * @details P14 proves the demon COMPUTES the same thing; this proves it DECIDES the same thing —
 *          which note it kept, which move it reached for, whether it finished or asked for
 *          help. Nothing here runs a model, which is what makes it replayable at all. Must match
 *          LplAssistant/tests/test_agency_parity.cpp.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p16_agency(Serial_t *com1)
{
    libassistant_agency_fold_result_t agency;
    libassistant_agency_fold(&agency);
    const SmokeLibengineRow_t agency_rows[] = {
        {"persona_sig=",      agency.persona_sig   },
        {", intent_sig=",     agency.intent_sig    },
        {", memory_sig=",     agency.memory_sig    },
        {", recall_sig=",     agency.recall_sig    },
        {", transcript_sig=", agency.transcript_sig},
        {", utterance_sig=",  agency.utterance_sig },
        {", budget_sig=",     agency.budget_sig    },
        {", intent_kind=",    agency.intent_kind   },
        {", dropped=",        agency.dropped       },
        {", notes_held=",     agency.notes_held    },
        {", evictions=",      agency.evictions     },
        {", refusals=",       agency.refusals      },
        {", recall_hits=",    agency.recall_hits   },
        {", lines=",          agency.lines         },
        {", steps=",          agency.steps         },
        {", tokens=",         agency.tokens        },
        {", utterance_kind=", agency.utterance_kind},
        {", satisfied=",      agency.satisfied     },
        {", world_refusals=", agency.world_refusals},
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: libassistant P16 agency: ", agency_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(agency_rows));
}

/**
 * @brief Gate P17: the same turn, every move chosen by the transformer running here.
 *
 * @details Under a grammar rebuilt from the world at each step. Must match
 *          LplAssistant/tests/test_agency_parity.cpp.
 *
 * @note `illegal` must be zero, and `free_legal` is what makes that mean something — the same
 *       weights generating unconstrained, and how often they land on a move the world would
 *       have accepted.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p17_reasoning(Serial_t *com1)
{
    libassistant_reasoning_fold_result_t reasoning;
    libassistant_reasoning_fold(&reasoning);
    const SmokeLibengineRow_t reasoning_rows[] = {
        {"reason_transcript_sig=",  reasoning.transcript_sig},
        {", reason_action_sig=",    reasoning.action_sig    },
        {", reason_utterance_sig=", reasoning.utterance_sig },
        {", reason_generations=",   reasoning.generations   },
        {", reason_completions=",   reasoning.completions   },
        {", reason_illegal=",       reasoning.illegal       },
        {", reason_exhausted=",     reasoning.exhausted     },
        {", reason_tokens=",        reasoning.tokens        },
        {", reason_lines=",         reasoning.lines         },
        {", reason_steps=",         reasoning.steps         },
        {", reason_satisfied=",     reasoning.satisfied     },
        {", reason_free_attempts=", reasoning.free_attempts },
        {", reason_free_legal=",    reasoning.free_legal    },
        {", reason_arena=",         reasoning.arena_bytes   },
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: libassistant P17 reasoning: ", reasoning_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(reasoning_rows));
}

#    endif /* !LPL_ASSISTANT_UNAVAILABLE */

#    if !defined(LPL_KNOWLEDGE_UNAVAILABLE)

/**
 * @brief Gate P18: the canonical corpus of P13, written to bytes by a host tool and read back
 *        HERE.
 *
 * @details Unlike every gate before it, what is being checked is a TRANSLATION — so
 *          `know_timeline_sig`, `know_chronicle_sig` and `know_minority_sig` must equal the
 *          records of the engine test `history.the_consensus_wins_and_the_myth_stays_traceable`,
 *          and `know_round_trip` must be 1. Must match LplKnowledge/tests/test_knowledge_parity.cpp.
 *
 * @note An image that opened cleanly and had rounded one confidence would pass everything else.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p18_corpus(Serial_t *com1)
{
    libknowledge_corpus_fold_result_t corpus;
    libknowledge_corpus_fold(&corpus);
    const SmokeLibengineRow_t corpus_rows[] = {
        {"know_image_sig=",       corpus.image_sig    },
        {", know_fact_sig=",      corpus.fact_sig     },
        {", know_vocab_sig=",     corpus.vocab_sig    },
        {", know_audit_sig=",     corpus.audit_sig    },
        {", know_page_sig=",      corpus.page_sig     },
        {", know_citation_sig=",  corpus.citation_sig },
        {", know_timeline_sig=",  corpus.timeline_sig },
        {", know_chronicle_sig=", corpus.chronicle_sig},
        {", know_minority_sig=",  corpus.minority_sig },
        {", know_bytes=",         corpus.image_bytes  },
        {", know_open_status=",   corpus.open_status  },
        {", know_sections=",      corpus.sections     },
        {", know_skipped=",       corpus.skipped      },
        {", know_facts=",         corpus.facts        },
        {", know_sources=",       corpus.sources      },
        {", know_documents=",     corpus.documents    },
        {", know_loci=",          corpus.loci         },
        {", know_names=",         corpus.names        },
        {", know_matched=",       corpus.matched      },
        {", know_returned=",      corpus.returned     },
        {", know_truncated=",     corpus.truncated    },
        {", know_consensus=",     corpus.consensus    },
        {", know_provenance=",    corpus.provenance   },
        {", know_round_trip=",    corpus.round_trip   },
        {", know_rejected=",      corpus.rejected     },
    };
    smoke_libengine_write_rows(com1, "[" KERNEL_SYSTEM_STRING "]: libknowledge P18 corpus: ", corpus_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(corpus_rows));
    serial_write_string(com1, ", know_image=");
    serial_write_string(com1, libknowledge_image_state());
    serial_write_string(com1, "\n");
}

#    endif /* !LPL_KNOWLEDGE_UNAVAILABLE */

void smoke_libengine_run_all(Serial_t *com1)
{
#    if !defined(LPL_ASSISTANT_UNAVAILABLE)
    smoke_libengine_report_p14_mind(com1);
    smoke_libengine_report_power_floor(com1);
    smoke_libengine_report_p15_satellite(com1);
    smoke_libengine_report_p16_agency(com1);
    smoke_libengine_report_p17_reasoning(com1);
#    endif /* !LPL_ASSISTANT_UNAVAILABLE */
#    if !defined(LPL_KNOWLEDGE_UNAVAILABLE)
    smoke_libengine_report_p18_corpus(com1);
#    endif /* !LPL_KNOWLEDGE_UNAVAILABLE */
}

#endif /* LPL_KERNEL_ENABLE_SMOKE_TESTS && (!LPL_ASSISTANT_UNAVAILABLE || !LPL_KNOWLEDGE_UNAVAILABLE) */
