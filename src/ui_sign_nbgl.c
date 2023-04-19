#ifdef HAVE_NBGL
#include "nbgl_use_case.h"
#include "apdu_sign.h"

#include "apdu.h"
#include "baking_auth.h"
#include "base58.h"
#include "globals.h"
#include "keys.h"
#include "memory.h"
#include "protocol.h"
#include "to_string.h"
#include "ui.h"
#include "swap/swap_lib_calls.h"

#include "cx.h"

#include <string.h>

#define G global.apdu.u.sign

#define PARSE_ERROR() THROW(EXC_PARSE_ERROR)


static void continue_callback(void);
static void prompt_cancel(void);

typedef struct {
    ui_callback_t ok_cb;
    ui_callback_t cxl_cb;
    nbgl_layoutTagValue_t tagValuePair[6];
    nbgl_layoutTagValueList_t tagValueList;
    nbgl_pageInfoLongPress_t infoLongPress;
    char* confirmed;
    char* cancelled;
    char buffer[6][100];
    uint8_t last_screen;
} TransactionContext_t;

enum {
    PROMPT_SCREEN,
    TX_SCREEN
};

static TransactionContext_t transactionContext;

static bool sign_without_hash_ok(void) {
    delayed_send(perform_signature(true, false));
    return true;
}

static bool sign_with_hash_ok(void) {
    delayed_send(perform_signature(true, true));
    return true;
}

static bool sign_reject(void) {
    delay_reject();
    return true;  // Return to idle
}

static void cancel_callback(void) {
    nbgl_useCaseStatus(transactionContext.cancelled, false, ui_initial_screen);
    transactionContext.cxl_cb();
}

static void approve_callback(void) {
    nbgl_useCaseStatus(transactionContext.confirmed, true, ui_initial_screen);
    transactionContext.ok_cb();
}

static void confirmation_callback(bool confirm) {
    if (confirm) {
        approve_callback();
    }
    else {
        cancel_callback();
    }
}

static void prompt_confirmation_callback(bool confirm) {
    if (confirm) {
        approve_callback();
    }
    else {
        prompt_cancel();
    }
}
static void cancel_confirmation_callback(bool confirm) {
    if (confirm) {
        confirmation_callback(false);
    }
    else {
        if (transactionContext.last_screen == PROMPT_SCREEN) {
            nbgl_useCaseReviewStart(&C_tezos, "Review Transaction", NULL, "Cancel", continue_callback, prompt_cancel);
        }
        else if (transactionContext.last_screen == TX_SCREEN) {
            continue_callback();
        }
    }
}

static void prompt_cancel(void) {
    nbgl_useCaseChoice(&C_round_warning_64px,"Reject transaction?", "", "Yes, Reject", "Go back to transaction", cancel_confirmation_callback);
}

static void continue_callback(void) {
    nbgl_layoutTagValueList_t list = {
        .nbPairs = transactionContext.tagValueList.nbPairs,
        .pairs = transactionContext.tagValuePair
    };

    transactionContext.last_screen = TX_SCREEN;
    transactionContext.infoLongPress.icon = &C_tezos;
    transactionContext.infoLongPress.longPressText = "Hold to sign";
    transactionContext.infoLongPress.tuneId = TUNE_TAP_CASUAL;

    nbgl_useCaseStaticReview(&list, &transactionContext.infoLongPress, "Cancel", prompt_confirmation_callback);
}

static void continue_light_callback(void) {
    nbgl_layoutTagValueList_t list = {
        .nbPairs = transactionContext.tagValueList.nbPairs,
        .pairs = transactionContext.tagValuePair
    };

    transactionContext.infoLongPress.icon = &C_tezos;
    transactionContext.infoLongPress.longPressText = "Approve";
    transactionContext.infoLongPress.tuneId = TUNE_TAP_CASUAL;

    nbgl_useCaseStaticReviewLight(&list, &transactionContext.infoLongPress, "Cancel", confirmation_callback);
}


#ifdef BAKING_APP  // ----------------------------------------------------------

void prompt_register_delegate(ui_callback_t const ok_cb,
                              ui_callback_t const cxl_cb) {
    if (!G.maybe_ops.is_valid) THROW(EXC_MEMORY_ERROR);

    transactionContext.ok_cb = ok_cb;
    transactionContext.cxl_cb = cxl_cb;

    bip32_path_with_curve_to_pkh_string(transactionContext.buffer[0], sizeof(transactionContext.buffer[0]), &global.path_with_curve);
    microtez_to_string_indirect(transactionContext.buffer[1], sizeof(transactionContext.buffer[1]), &G.maybe_ops.v.total_fee);

    transactionContext.tagValuePair[0].item = "Address";
    transactionContext.tagValuePair[0].value = transactionContext.buffer[0];

    transactionContext.tagValuePair[1].item = "Fee";
    transactionContext.tagValuePair[1].value = transactionContext.buffer[1];

    transactionContext.tagValueList.nbPairs = 2;

    transactionContext.infoLongPress.text = "Confirm delegate\nregistration";

    transactionContext.confirmed = "DELEGATE\nCONFIRMED";
    transactionContext.cancelled = "Delegate registration\ncancelled";

    nbgl_useCaseReviewStart(&C_tezos, "Register delegate", NULL, "Cancel", continue_light_callback, cancel_callback);
}

#else  // ifdef BAKING_APP -----------------------------------------------------

static bool sign_unsafe_ok(void) {
    delayed_send(perform_signature(false, false));
    return true;
}

#define MAX_NUMBER_CHARS (MAX_INT_DIGITS + 2)  // include decimal point and terminating null
bool prompt_transaction(struct parsed_operation_group const *const ops,
                        bip32_path_with_curve_t const *const key,
                        ui_callback_t ok,
                        ui_callback_t cxl) {
    check_null(ops);
    check_null(key);

    if (called_from_swap) {
        if (is_safe_to_swap() == true) {
            // We're called from swap and we've verified that the data is correct. Sign it.
            ok();
            // Exit properly.
            os_sched_exit(0);
        } else {
            // Send the error message back in response.
            cxl();
            // Exit with error code.
            os_sched_exit(1);
        }
    }

    switch (ops->operation.tag) {
        default:
            PARSE_ERROR();

        case OPERATION_TAG_PROPOSAL: {

            transactionContext.ok_cb = ok;
            transactionContext.cxl_cb = cxl;

            parsed_contract_to_string(transactionContext.buffer[0], sizeof(transactionContext.buffer[0]), &ops->operation.source);
            number_to_string_indirect32(transactionContext.buffer[1], sizeof(transactionContext.buffer[1]), &ops->operation.proposal.voting_period);
            protocol_hash_to_string(transactionContext.buffer[2], sizeof(transactionContext.buffer[2]), ops->operation.proposal.protocol_hash);

            transactionContext.tagValuePair[0].item = "Source";
            transactionContext.tagValuePair[0].value = transactionContext.buffer[0];

            transactionContext.tagValuePair[1].item = "Period";
            transactionContext.tagValuePair[1].value = transactionContext.buffer[1];

            transactionContext.tagValuePair[2].item = "Protocol";
            transactionContext.tagValuePair[2].value = transactionContext.buffer[2];

            transactionContext.tagValueList.nbPairs = 3;

            transactionContext.infoLongPress.text = "Confirm proposal";

            transactionContext.confirmed = "PROPOSAL\nCONFIRMED";
            transactionContext.cancelled = "Proposal\ncancelled";

            nbgl_useCaseReviewStart(&C_tezos, "Review proposal", NULL, "Cancel", continue_light_callback, cancel_callback);
            break;
        }

        case OPERATION_TAG_BALLOT: {
            char *vote;

            switch (ops->operation.ballot.vote) {
                case BALLOT_VOTE_YEA:
                    vote = "Yea";
                    break;
                case BALLOT_VOTE_NAY:
                    vote = "Nay";
                    break;
                case BALLOT_VOTE_PASS:
                    vote = "Pass";
                    break;
            }

            transactionContext.ok_cb = ok;
            transactionContext.cxl_cb = cxl;

            parsed_contract_to_string(transactionContext.buffer[0], sizeof(transactionContext.buffer[0]), &ops->operation.source);
            protocol_hash_to_string(transactionContext.buffer[1], sizeof(transactionContext.buffer[1]), ops->operation.ballot.protocol_hash);
            number_to_string_indirect32(transactionContext.buffer[2], sizeof(transactionContext.buffer[2]), &ops->operation.ballot.voting_period);

            transactionContext.tagValuePair[0].item = "Vote";
            transactionContext.tagValuePair[0].value = vote;

            transactionContext.tagValuePair[1].item = "Source";
            transactionContext.tagValuePair[1].value = transactionContext.buffer[0];

            transactionContext.tagValuePair[2].item = "Protocol";
            transactionContext.tagValuePair[2].value = transactionContext.buffer[1];

            transactionContext.tagValuePair[3].item = "Period";
            transactionContext.tagValuePair[3].value = transactionContext.buffer[2];

            transactionContext.tagValueList.nbPairs = 4;

            transactionContext.infoLongPress.text = "Confirm vote";

            transactionContext.confirmed = "VOTE\nCONFIRMED";
            transactionContext.cancelled = "Vote\ncancelled";

            nbgl_useCaseReviewStart(&C_tezos, "Review vote", NULL, "Cancel", continue_light_callback, cancel_callback);
            break;
        }

        case OPERATION_TAG_ATHENS_ORIGINATION:
        case OPERATION_TAG_BABYLON_ORIGINATION: {
            if (!(ops->operation.flags & ORIGINATION_FLAG_SPENDABLE)) return false;

            transactionContext.ok_cb = ok;
            transactionContext.cxl_cb = cxl;

            microtez_to_string_indirect(transactionContext.buffer[0], sizeof(transactionContext.buffer[0]), &ops->operation.amount);
            microtez_to_string_indirect(transactionContext.buffer[1], sizeof(transactionContext.buffer[1]), &ops->total_fee);
            parsed_contract_to_string(transactionContext.buffer[2], sizeof(transactionContext.buffer[2]), &ops->operation.source);
            parsed_contract_to_string(transactionContext.buffer[3], sizeof(transactionContext.buffer[3]), &ops->operation.destination);

            transactionContext.tagValuePair[0].item = "Amount";
            transactionContext.tagValuePair[0].value = transactionContext.buffer[0];

            transactionContext.tagValuePair[1].item = "Fee";
            transactionContext.tagValuePair[1].value = transactionContext.buffer[1];

            transactionContext.tagValuePair[2].item = "Source";
            transactionContext.tagValuePair[2].value = transactionContext.buffer[2];

            transactionContext.tagValuePair[3].item = "Manager";
            transactionContext.tagValuePair[3].value = transactionContext.buffer[3];

            transactionContext.tagValuePair[4].value = transactionContext.buffer[4];

            bool const delegatable = ops->operation.flags & ORIGINATION_FLAG_DELEGATABLE;
            bool const has_delegate =
                ops->operation.delegate.signature_type != SIGNATURE_TYPE_UNSET;
            if (delegatable && has_delegate) {
                transactionContext.tagValuePair[4].item = "Delegate";
                parsed_contract_to_string(transactionContext.buffer[4], sizeof(transactionContext.buffer[4]), &ops->operation.delegate);
            } else if (delegatable && !has_delegate) {
                transactionContext.tagValuePair[4].item = "Delegate";
                transactionContext.tagValuePair[4].value = "Any";
            } else if (!delegatable && has_delegate) {
                transactionContext.tagValuePair[4].item = "Fixed Delegate";
                parsed_contract_to_string(transactionContext.buffer[4], sizeof(transactionContext.buffer[4]), &ops->operation.delegate);
            } else if (!delegatable && !has_delegate) {
                transactionContext.tagValuePair[4].item = "Delegation Disabled";
                transactionContext.tagValuePair[4].value = "No delegation";
            }

            number_to_string_indirect64(transactionContext.buffer[5], sizeof(transactionContext.buffer[5]), &ops->total_storage_limit);

            transactionContext.tagValuePair[5].item = "Storage Limit";
            transactionContext.tagValuePair[5].value = transactionContext.buffer[5];

            transactionContext.tagValueList.nbPairs = 6;

            transactionContext.infoLongPress.text = "Confirm origination";

            transactionContext.confirmed = "ORIGINATION\nCONFIRMED";
            transactionContext.cancelled = "Origination\ncancelled";

            nbgl_useCaseReviewStart(&C_tezos, "Review origination", NULL, "Cancel", continue_light_callback, cancel_callback);
            break;
        }
        case OPERATION_TAG_ATHENS_DELEGATION:
        case OPERATION_TAG_BABYLON_DELEGATION: {
            bool const withdrawal =
                ops->operation.destination.originated == 0 &&
                ops->operation.destination.signature_type == SIGNATURE_TYPE_UNSET;

            char *type_msg;
            if (withdrawal) {
                transactionContext.confirmed = "DELEGATION\nCONFIRMED";
                transactionContext.cancelled = "Delegation withdrawal\ncancelled";

                type_msg = "Withdraw Delegation";
            } else {
                transactionContext.confirmed = "DELEGATION\nCONFIRMED";
                transactionContext.cancelled = "Delegation\ncancelled";

                type_msg = "Confirm Delegation";
            }

            transactionContext.ok_cb = ok;
            transactionContext.cxl_cb = cxl;

            microtez_to_string_indirect(transactionContext.buffer[0], sizeof(transactionContext.buffer[0]), &ops->total_fee);
            parsed_contract_to_string(transactionContext.buffer[1], sizeof(transactionContext.buffer[1]), &ops->operation.source);
            parsed_contract_to_string(transactionContext.buffer[2], sizeof(transactionContext.buffer[2]), &ops->operation.destination);
            lookup_parsed_contract_name(transactionContext.buffer[3], sizeof(transactionContext.buffer[3]), &ops->operation.destination);
            number_to_string_indirect64(transactionContext.buffer[4], sizeof(transactionContext.buffer[4]), &ops->total_storage_limit);

            transactionContext.tagValuePair[0].item = "Fee";
            transactionContext.tagValuePair[0].value = transactionContext.buffer[0];

            transactionContext.tagValuePair[1].item = "Source";
            transactionContext.tagValuePair[1].value = transactionContext.buffer[1];

            transactionContext.tagValuePair[2].item = "Delegate";
            transactionContext.tagValuePair[2].value = transactionContext.buffer[2];

            transactionContext.tagValuePair[3].item = "Delegate Name";
            transactionContext.tagValuePair[3].value = transactionContext.buffer[3];

            transactionContext.tagValuePair[4].item = "Storage limit";
            transactionContext.tagValuePair[4].value = transactionContext.buffer[4];
            transactionContext.tagValueList.nbPairs = 5;

            transactionContext.infoLongPress.text = type_msg;

            nbgl_useCaseReviewStart(&C_tezos, type_msg, NULL, "Cancel", continue_light_callback, cancel_callback);
            break;
        }

        case OPERATION_TAG_ATHENS_TRANSACTION:
        case OPERATION_TAG_BABYLON_TRANSACTION: {

            transactionContext.ok_cb = ok;
            transactionContext.cxl_cb = cxl;

            microtez_to_string_indirect(transactionContext.buffer[0], sizeof(transactionContext.buffer[0]), &ops->operation.amount);
            microtez_to_string_indirect(transactionContext.buffer[1], sizeof(transactionContext.buffer[1]), &ops->total_fee);
            parsed_contract_to_string(transactionContext.buffer[2], sizeof(transactionContext.buffer[2]), &ops->operation.source);
            parsed_contract_to_string(transactionContext.buffer[3], sizeof(transactionContext.buffer[3]), &ops->operation.destination);
            number_to_string_indirect64(transactionContext.buffer[4], sizeof(transactionContext.buffer[4]), &ops->total_storage_limit);

            transactionContext.tagValuePair[0].item = "Amount";
            transactionContext.tagValuePair[0].value = transactionContext.buffer[0];

            transactionContext.tagValuePair[1].item = "Fee";
            transactionContext.tagValuePair[1].value = transactionContext.buffer[1];

            transactionContext.tagValuePair[2].item = "Source";
            transactionContext.tagValuePair[2].value = transactionContext.buffer[2];

            transactionContext.tagValuePair[3].item = "Destination";
            transactionContext.tagValuePair[3].value = transactionContext.buffer[3];

            transactionContext.tagValuePair[4].item = "Storage limit";
            transactionContext.tagValuePair[4].value = transactionContext.buffer[4];

            transactionContext.tagValueList.nbPairs = 5;

            transactionContext.infoLongPress.text = "Sign transaction to\nsend Tezos?";

            transactionContext.confirmed = "TRANSACTION\nSIGNED";
            transactionContext.cancelled = "Transaction\nrejected";
            transactionContext.last_screen = PROMPT_SCREEN;

            nbgl_useCaseReviewStart(&C_tezos, "Review Transaction", NULL, "Cancel", continue_callback, prompt_cancel);
            break;
        }
        case OPERATION_TAG_NONE: {

            transactionContext.ok_cb = ok;
            transactionContext.cxl_cb = cxl;

            parsed_contract_to_string(transactionContext.buffer[0], sizeof(transactionContext.buffer[0]), &ops->operation.source);
            microtez_to_string_indirect(transactionContext.buffer[1], sizeof(transactionContext.buffer[1]), &ops->total_fee);
            number_to_string_indirect64(transactionContext.buffer[2], sizeof(transactionContext.buffer[2]), &ops->total_storage_limit);


            transactionContext.tagValuePair[0].item = "Key";
            transactionContext.tagValuePair[0].value = transactionContext.buffer[0];

            transactionContext.tagValuePair[1].item = "Fee";
            transactionContext.tagValuePair[1].value = transactionContext.buffer[1];

            transactionContext.tagValuePair[2].item = "Storage limit";
            transactionContext.tagValuePair[2].value = transactionContext.buffer[2];

            transactionContext.tagValueList.nbPairs = 3;

            transactionContext.infoLongPress.text = "Confirm key revelation";

            transactionContext.confirmed = "KEY REVELATION\nCONFIRMED";
            transactionContext.cancelled = "Key revelation\nrejected";

            nbgl_useCaseReviewStart(&C_tezos, "Reveal key to\nBlockchain", NULL, "Cancel", continue_light_callback, cancel_callback);
            break;
        }
    }
    return 0;
}

size_t wallet_sign_complete(uint8_t instruction, uint8_t magic_byte, volatile uint32_t* flags) {
    char *ops;
    if (magic_byte == MAGIC_BYTE_UNSAFE_OP3) {
        ops = "Michelson";
    } else {
        ops = "Operation";
    }

    if (instruction == INS_SIGN_UNSAFE) {
        G.message_data_as_buffer.bytes = (uint8_t *) &G.message_data;
        G.message_data_as_buffer.size = sizeof(G.message_data);
        G.message_data_as_buffer.length = G.message_data_length;

        buffer_to_base58(transactionContext.buffer[0], sizeof(transactionContext.buffer[0]), &G.message_data_as_buffer);

        transactionContext.tagValuePair[0].item = "Sign Hash";
        transactionContext.tagValuePair[0].value = transactionContext.buffer[0];
        transactionContext.tagValueList.nbPairs = 1;

        transactionContext.ok_cb = sign_unsafe_ok;
        transactionContext.cxl_cb = sign_reject;

        transactionContext.infoLongPress.text = "Confirm";

        nbgl_useCaseReviewStart(&C_tezos, "Review pre-hashed", ops, "Cancel", continue_light_callback, cancel_callback);
        *flags = IO_ASYNCH_REPLY;
        return 0;
    } else {
        ui_callback_t const ok_c =
            instruction == INS_SIGN_WITH_HASH ? sign_with_hash_ok : sign_without_hash_ok;

        switch (G.magic_byte) {
            case MAGIC_BYTE_BLOCK:
            case MAGIC_BYTE_BAKING_OP:
            default:
                PARSE_ERROR();
            case MAGIC_BYTE_UNSAFE_OP:
                if (!G.maybe_ops.is_valid || prompt_transaction(&G.maybe_ops.v,
                                                                 &global.path_with_curve,
                                                                 ok_c,
                                                                 sign_reject)) {
                    goto unsafe;
                }
                *flags = IO_ASYNCH_REPLY;
                return 0;

            case MAGIC_BYTE_UNSAFE_OP2:
            case MAGIC_BYTE_UNSAFE_OP3:
                goto unsafe;
        }
    unsafe:
        G.message_data_as_buffer.bytes = (uint8_t *) &G.final_hash;
        G.message_data_as_buffer.size = sizeof(G.final_hash);
        G.message_data_as_buffer.length = sizeof(G.final_hash);

        if (magic_byte == MAGIC_BYTE_UNSAFE_OP3) {
            ops = "Review unrecognized\nPre-hashed Michelson";
            transactionContext.infoLongPress.text = "Confirm unrecognized\nPre-hashed Michelson";
            transactionContext.confirmed = "PRE-HASHED\nMICHELSON\nCONFIRMED";
            transactionContext.cancelled = "Pre-hashed michelson\nrejected";

        } else {
            ops = "Review unrecognized\nPre-hashed Operation";
            transactionContext.infoLongPress.text = "Confirm unrecognized\nPre-hashed Operation";
            transactionContext.confirmed = "PRE-HASHED\nOPERATION\nCONFIRMED";
            transactionContext.cancelled = "Pre-hashed operation\nrejected";
        }


        buffer_to_base58(transactionContext.buffer[0], sizeof(transactionContext.buffer[0]), &G.message_data_as_buffer);

        transactionContext.tagValuePair[0].item = "Sign Hash";
        transactionContext.tagValuePair[0].value = transactionContext.buffer[0];

        transactionContext.tagValueList.nbPairs = 1;

        transactionContext.ok_cb = ok_c;
        transactionContext.cxl_cb = sign_reject;

        nbgl_useCaseReviewStart(&C_tezos, ops, NULL, "Cancel", continue_light_callback, cancel_callback);

        *flags = IO_ASYNCH_REPLY;
        return 0;
    }
}

#endif  // ifdef BAKING_APP ----------------------------------------------------
#endif // HAVE_NBGL
