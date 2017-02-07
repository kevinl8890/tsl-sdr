#pragma once

#include <tsl/result.h>

#include <stdint.h>

struct pager_flex;

enum pager_flex_msg_type {
    PAGER_FLEX_UNKNOWN,
    PAGER_FLEX_ALPHANUMERIC,
    PAGER_FLEX_NUMERIC,
    PAGER_FLEX_TONE,
};

enum pager_flex_modulation {
    PAGER_FLEX_MODULATION_2FSK,
    PAGER_FLEX_MODULATION_4FSK
};

enum pager_flex_state {
    /**
     * We're hunting for a sync pattern for Sync 1. This includes searching for
     * the alternating 1/0 pattern, while also monitoring for any of the 16-bit
     * Sync A codes.
     *
     * This uses the alternating 1/0 pattern at 1600 bps to increase confidence
     * of a sync match. The 1/0 pattern is not a discriminating pattern.
     */
    PAGER_FLEX_STATE_SYNC_1,

    /**
     * We've found the sync pattern, verified it, eaten the FIW, and are now entering
     * the second Sync phase. This is where we train the 4FSK slicer.
     */
    PAGER_FLEX_STATE_SYNC_2,

    /**
     * We're now decoding blocks of this frame
     */
    PAGER_FLEX_STATE_BLOCK,
};

/**
 * Callback type. This is registered with each pager_flex, and is called whenever there is a message to process.
 *
 * Provides the callee with the baud rate, the phase ID, message type and the cap code and the message decoded as
 * ASCII.
 *
 * \ul If the message type is TONE, then the message_len is 0 and message_bytes is NULL
 * \ul If the message type is NUMERIC, the message_bytes is an ASCII representation of the numeric page
 * \ul If the message type is ALPHANUMERIC, the message_bytes is an ASCII representation of the message
 * \ul If the message type is UNKNOWN, then message_bytes is the post-BCH processed FLEX page words.
 */
typedef aresult_t (*pager_flex_on_message_cb_t)(struct pager_flex *flex, uint16_t baud, char phase, uint32_t cap_code, enum pager_flex_msg_type message_type, const char *message_bytes, size_t message_len);

enum pager_flex_sync_state {
    /**
     * Searching for the Bitsync 1 pattern
     */
    PAGER_FLEX_SYNC_STATE_SEARCH_BS1,

    /**
     * Found the Bitsync 1 pattern, 32-bits long
     */
    PAGER_FLEX_SYNC_STATE_BS1,

    /**
     * Looking for the A word of the sync. 32-bits long, 16-bits indicating the state, 16-bits being constant
     */
    PAGER_FLEX_SYNC_STATE_A,

    /**
     * Looking for the B word (not strict) - 16 bits
     */
    PAGER_FLEX_SYNC_STATE_B,

    /**
     * Looking for the A mode word, inverted - 32-bits
     */
    PAGER_FLEX_SYNC_STATE_INV_A,

    /**
     * Accumulating the FIW
     */
    PAGER_FLEX_SYNC_STATE_FIW,

    /**
     * That's it. Once we have the FIW, we check all the state pieces. If they check out, we can expose
     * state to the FLEX pager object itself. If they don't we reset to BS1 state. The FLEX pager object
     * will then switch to Sync 2 if everything checks out.
     */
    PAGER_FLEX_SYNC_STATE_SYNCED,
};

struct pager_flex_coding;

/**
 * FLEX Sync 1 stage state tracker. Tracks the detection of various sync phases in Sync 1,
 * then stores the current state for the rest of the objects to extract.
 */
struct pager_flex_sync {
    uint32_t sync_words[10];
    enum pager_flex_sync_state state;
    uint8_t sample_counter;
    uint8_t bit_counter;
    uint32_t a;
    uint16_t b;
    uint32_t inv_a;
    uint32_t fiw;
    struct pager_flex_coding *coding;
};

enum pager_flex_sync_2_state {
    /**
     * Accumulate comma values. We calculate the envelope of the signal during this period.
     */
    PAGER_FLEX_SYNC_2_STATE_COMMA,

    /**
     * Accumulate the C pattern
     */
    PAGER_FLEX_SYNC_2_STATE_C,

    /**
     * Accumulate the inverted comma
     */
    PAGER_FLEX_SYNC_2_STATE_INV_COMMA,

    /**
     * Accumulate inverted C
     */
    PAGER_FLEX_SYNC_2_STATE_INV_C,

    /**
     * We're close enough on the C pattern now, let's start handling the block.
     */
    PAGER_FLEX_SYNC_2_STATE_SYNCED,
};

/**
 * FLEX Sync 2 stage state tracker. Tracks the comma and the C pattern.
 *
 * This also detects the envelope of the signal, to train the 4FSK slicer.
 */
struct pager_flex_sync_2 {
    /**
     * Current state of Sync 2 decoding
     */
    enum pager_flex_sync_2_state state;

    /**
     * The count of the number of dots we've seen
     */
    uint16_t nr_dots;

    /**
     * The C value we've accumulated (diagnostic only)
     */
    uint16_t c;

    /**
     * Number of bits of C we've processed
     */
    uint8_t nr_c;

    /**
     * Sum of samples in dot sequence, high.
     */
    int32_t range_avg_sum_high;

    /**
     * Sum of samples in dot sequence, low
     */
    int32_t range_avg_sum_low;

    /**
     * 
     */
    unsigned range_avg_count_high;

    /**
     *
     */
    unsigned range_avg_count_low;
};

struct bch_code;

/**
 * A FLEX pager decoder.
 *
 * The input for this must always be a 16kHz signal.
 */
struct pager_flex {
    /**
     * Quantized sample max.
     */
    int16_t slice_range_high;

    /**
     * Quantized sample min
     */
    int16_t slice_range_low;

    /**
     * Callback hit on a complete message
     */
    pager_flex_on_message_cb_t on_msg;

    /**
     * Synchronization state for the FLEX message stream
     */
    struct pager_flex_sync sync;

    /**
     * State for the second phase of the synchronization.
     */
    struct pager_flex_sync_2 sync_2;

    /**
     * State for the BCH Error Corrector for the BCH(31, 23) code FLEX uses
     */
    struct bch_code *bch;

    /**
     * The baud rate
     */
    uint16_t baud_rate;

    /**
     * Current modulation. This always starts in 2FSK, but can move to 4FSK depending on the sync word contents.
     */
    enum pager_flex_modulation modulation;

    /**
     * Symbol counter. This is used to count symbols dependant on the state the FLEX receiver is in
     */
    unsigned int symbol_counter;

    /**
     * The current state of the FLEX receiver
     */
    enum pager_flex_state state;

    /**
     * The number of samples to skip before sampling for slicing
     */
    uint16_t skip;

    /**
     * The skip count
     */
    uint16_t skip_count;

    /**
     * The symbol sample rate. The number of samples that represents a single symbol
     */
    uint16_t symbol_samples;

    /**
     * Frequency, in Hertz, of the center of this pager channel
     */
    uint32_t freq_hz;
};

/**
 * Create a new FLEX pager handler.
 *
 * \param pflex The new FLEX pager handler. Returned by reference.
 * \param freq_hz The frequency, in Hertz, of this channel. Used for record keeping mostly.
 * \param on_msg Callback hit by the FLEX pager handler whenever a finished message is ready to be processed.
 *
 * \return A_OK on success, an error code otherwise.
 */
aresult_t pager_flex_new(struct pager_flex **pflex, uint32_t freq_hz, pager_flex_on_message_cb_t on_msg);

/**
 * Delete a FLEX pager handler.
 *
 * \param pflex The FLEX pager handler to delete. Passed by reference. Set to NULL when cleanup is finished.
 *
 * \return A_OK on success, an error code otherwise.
 */
aresult_t pager_flex_delete(struct pager_flex **pflex);

/**
 * Push a block of PCM samples through the FLEX pager decoder. This will decode and demodulate/deliver data
 * as soon as enough data is available.
 *
 * \param flex The FLEX pager decoder state
 * \param pcm_samples PCM samples, in Q.15
 * \param nr_samples The number of samples to process in pcm_samples
 */
aresult_t pager_flex_on_pcm(struct pager_flex *flex, const int16_t *pcm_samples, size_t nr_samples);

