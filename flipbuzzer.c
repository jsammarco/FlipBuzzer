#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_pwm.h>
#include <furi_hal_speaker.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/text_input.h>
#include <input/input.h>
#include <storage/storage.h>
#include <dialogs/dialogs.h>
#include <lib/toolbox/path.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define FLIPBUZZER_APP_NAME "FlipBuzzer"
#define FLIPBUZZER_SOUND_DIR "/ext/apps_data/flipbuzzer"
#define FLIPBUZZER_SOUND_EXT ".fbsnd"
#define FLIPBUZZER_PLAYBACK_TICK_MS 50U
#define FLIPBUZZER_INPUT_QUEUE_SIZE 8
#define FLIPBUZZER_MIN_FREQ 20U
#define FLIPBUZZER_MAX_FREQ 20000U
#define FLIPBUZZER_MIN_DUTY 1U
#define FLIPBUZZER_MAX_DUTY 99U
#define FLIPBUZZER_DEFAULT_FREQ 1000U
#define FLIPBUZZER_DEFAULT_DUTY 50U
#define FLIPBUZZER_SERVO_FREQ 50U
#define FLIPBUZZER_SERVO_MIN_ANGLE 0U
#define FLIPBUZZER_SERVO_MAX_ANGLE 180U
#define FLIPBUZZER_SERVO_DEFAULT_ANGLE 90U
#define FLIPBUZZER_SERVO_MIN_PULSE_US 1000U
#define FLIPBUZZER_SERVO_MAX_PULSE_US 2000U
#define FLIPBUZZER_MAX_NOTES 128U
#define FLIPBUZZER_STATUS_LEN 64U
#define FLIPBUZZER_MORSE_TEXT_LEN 64U
#define FLIPBUZZER_FILE_NAME_LEN 32U
#define FLIPBUZZER_SPEAKER_ACQUIRE_TIMEOUT_MS 50U
#define FLIPBUZZER_INTERNAL_VOLUME 0.8f

typedef enum {
    FlipBuzzerViewMain,
    FlipBuzzerViewTextInput,
} FlipBuzzerViewId;

typedef enum {
    FlipBuzzerScreenMainMenu,
    FlipBuzzerScreenOutputMode,
    FlipBuzzerScreenFrequencyGenerator,
    FlipBuzzerScreenServoControl,
    FlipBuzzerScreenSavedSoundsMenu,
    FlipBuzzerScreenFilePlayback,
    FlipBuzzerScreenMorseCode,
    FlipBuzzerScreenAbout,
} FlipBuzzerScreen;

typedef enum {
    FlipBuzzerMainMenuOutputMode,
    FlipBuzzerMainMenuFrequencyGenerator,
    FlipBuzzerMainMenuServoControl,
    FlipBuzzerMainMenuSavedSounds,
    FlipBuzzerMainMenuMorseCode,
    FlipBuzzerMainMenuAbout,
    FlipBuzzerMainMenuCount,
} FlipBuzzerMainMenuItem;

typedef enum {
    FlipBuzzerCustomEventMorseTextReady = 1,
} FlipBuzzerCustomEvent;

typedef struct {
    void* app;
} FlipBuzzerMainViewModel;

typedef struct FlipBuzzerApp FlipBuzzerApp;

typedef enum {
    FlipBuzzerSavedSoundBuiltinStartup,
    FlipBuzzerSavedSoundBuiltinAlert,
    FlipBuzzerSavedSoundBrowseFiles,
    FlipBuzzerSavedSoundCount,
} FlipBuzzerSavedSoundItem;

typedef enum {
    FlipBuzzerOutputExternal,
    FlipBuzzerOutputInternal,
    FlipBuzzerOutputBoth,
    FlipBuzzerOutputCount,
} FlipBuzzerOutputMode;

typedef struct {
    uint16_t frequency;
    uint16_t duration_ms;
    uint8_t duty;
} FlipBuzzerToneStep;

struct FlipBuzzerApp {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    View* main_view;
    TextInput* text_input;
    Storage* storage;
    DialogsApp* dialogs;
    NotificationApp* notification;

    FlipBuzzerScreen screen;
    uint8_t main_menu_index;
    uint8_t saved_sound_index;
    uint32_t generator_frequency;
    uint8_t generator_duty;
    uint8_t servo_angle;
    FlipBuzzerToneStep file_steps[FLIPBUZZER_MAX_NOTES];
    size_t file_step_count;
    size_t file_step_index;
    uint32_t file_step_elapsed_ms;
    uint32_t file_elapsed_ms;
    uint32_t file_total_ms;
    FlipBuzzerOutputMode output_mode;
    bool generator_playing;
    bool servo_active;
    bool file_playback_active;
    bool file_playback_paused;
    bool file_playback_pending_start;
    bool speaker_acquired;
    bool running;
    char status[FLIPBUZZER_STATUS_LEN];
    char morse_text[FLIPBUZZER_MORSE_TEXT_LEN];
    char current_file_name[FLIPBUZZER_FILE_NAME_LEN];
};

static void flipbuzzer_handle_main_menu(FlipBuzzerApp* app, const InputEvent* event);
static void flipbuzzer_handle_generator(FlipBuzzerApp* app, const InputEvent* event);
static void flipbuzzer_handle_servo_control(FlipBuzzerApp* app, const InputEvent* event);
static void flipbuzzer_handle_saved_sounds(FlipBuzzerApp* app, const InputEvent* event);
static void flipbuzzer_handle_file_playback(FlipBuzzerApp* app, const InputEvent* event);
static void flipbuzzer_tick_callback(void* context);

static const FlipBuzzerToneStep flipbuzzer_startup_sound[] = {
    {784, 80, 50},
    {988, 80, 50},
    {1319, 140, 50},
};

static const FlipBuzzerToneStep flipbuzzer_alert_sound[] = {
    {880, 140, 50},
    {0, 50, 0},
    {880, 140, 50},
    {0, 50, 0},
    {1175, 240, 50},
};

static const char* flipbuzzer_main_menu_items[FlipBuzzerMainMenuCount] = {
    "Output Mode",
    "Frequency Generator",
    "Servo Control",
    "Saved Sounds",
    "Morse Code",
    "About",
};

static const char* flipbuzzer_saved_sound_items[FlipBuzzerSavedSoundCount] = {
    "Startup Chime",
    "Alert Beep",
    "Browse Sound Files",
};

static const char* flipbuzzer_output_mode_labels[FlipBuzzerOutputCount] = {
    "External (A7)",
    "Internal",
    "Both",
};

static uint32_t flipbuzzer_clamp_frequency(uint32_t frequency) {
    if(frequency < FLIPBUZZER_MIN_FREQ) return FLIPBUZZER_MIN_FREQ;
    if(frequency > FLIPBUZZER_MAX_FREQ) return FLIPBUZZER_MAX_FREQ;
    return frequency;
}

static uint8_t flipbuzzer_clamp_duty(uint8_t duty) {
    if(duty < FLIPBUZZER_MIN_DUTY) return FLIPBUZZER_MIN_DUTY;
    if(duty > FLIPBUZZER_MAX_DUTY) return FLIPBUZZER_MAX_DUTY;
    return duty;
}

static uint8_t flipbuzzer_clamp_servo_angle(uint8_t angle) {
    if(angle > FLIPBUZZER_SERVO_MAX_ANGLE) return FLIPBUZZER_SERVO_MAX_ANGLE;
    return angle;
}

static void flipbuzzer_set_status(FlipBuzzerApp* app, const char* text) {
    furi_assert(app);
    snprintf(app->status, sizeof(app->status), "%s", text ? text : "");
}

static void flipbuzzer_main_view_update(FlipBuzzerApp* app) {
    view_commit_model(app->main_view, true);
}

static const char* flipbuzzer_get_output_label(const FlipBuzzerApp* app) {
    furi_assert(app);
    return flipbuzzer_output_mode_labels[app->output_mode];
}

static bool flipbuzzer_internal_start(FlipBuzzerApp* app, uint32_t frequency) {
    furi_assert(app);

    if(!app->speaker_acquired) {
        app->speaker_acquired =
            furi_hal_speaker_acquire(FLIPBUZZER_SPEAKER_ACQUIRE_TIMEOUT_MS);
    }

    if(app->speaker_acquired) {
        furi_hal_speaker_start((float)frequency, FLIPBUZZER_INTERNAL_VOLUME);
    }

    return app->speaker_acquired;
}

static void flipbuzzer_internal_stop(FlipBuzzerApp* app) {
    furi_assert(app);

    if(app->speaker_acquired) {
        furi_hal_speaker_stop();
        furi_hal_speaker_release();
        app->speaker_acquired = false;
    }
}

static void flipbuzzer_pwm_stop(FlipBuzzerApp* app) {
    furi_assert(app);
    if(furi_hal_pwm_is_running(FuriHalPwmOutputIdTim1PA7)) {
        furi_hal_pwm_stop(FuriHalPwmOutputIdTim1PA7);
    }
    flipbuzzer_internal_stop(app);
    app->generator_playing = false;
    app->servo_active = false;
}

static void flipbuzzer_led_blink_start(FlipBuzzerApp* app) {
    furi_assert(app);
    notification_message(app->notification, &sequence_blink_green_100);
}

static void flipbuzzer_led_blink_stop(FlipBuzzerApp* app) {
    furi_assert(app);
    notification_message(app->notification, &sequence_blink_stop);
    notification_message(app->notification, &sequence_reset_green);
}

static void flipbuzzer_pwm_start(FlipBuzzerApp* app, uint32_t frequency, uint8_t duty) {
    furi_assert(app);
    frequency = flipbuzzer_clamp_frequency(frequency);
    duty = flipbuzzer_clamp_duty(duty);

    bool external_started = false;
    bool internal_started = false;

    if(app->output_mode != FlipBuzzerOutputInternal) {
        if(furi_hal_pwm_is_running(FuriHalPwmOutputIdTim1PA7)) {
            furi_hal_pwm_set_params(FuriHalPwmOutputIdTim1PA7, frequency, duty);
        } else {
            furi_hal_pwm_start(FuriHalPwmOutputIdTim1PA7, frequency, duty);
        }
        external_started = furi_hal_pwm_is_running(FuriHalPwmOutputIdTim1PA7);
    } else if(furi_hal_pwm_is_running(FuriHalPwmOutputIdTim1PA7)) {
        furi_hal_pwm_stop(FuriHalPwmOutputIdTim1PA7);
    }

    if(app->output_mode != FlipBuzzerOutputExternal) {
        internal_started = flipbuzzer_internal_start(app, frequency);
    }

    app->generator_playing = external_started || internal_started;
}

static uint8_t flipbuzzer_servo_duty_from_angle(uint8_t angle) {
    uint32_t pulse_us = FLIPBUZZER_SERVO_MIN_PULSE_US +
                        ((uint32_t)angle * (FLIPBUZZER_SERVO_MAX_PULSE_US -
                                            FLIPBUZZER_SERVO_MIN_PULSE_US) /
                         FLIPBUZZER_SERVO_MAX_ANGLE);
    uint32_t duty = (pulse_us * 100U) / 20000U;
    if(duty < FLIPBUZZER_MIN_DUTY) duty = FLIPBUZZER_MIN_DUTY;
    if(duty > FLIPBUZZER_MAX_DUTY) duty = FLIPBUZZER_MAX_DUTY;
    return (uint8_t)duty;
}

static void flipbuzzer_servo_start(FlipBuzzerApp* app, uint8_t angle) {
    furi_assert(app);

    angle = flipbuzzer_clamp_servo_angle(angle);
    flipbuzzer_internal_stop(app);

    uint8_t duty = flipbuzzer_servo_duty_from_angle(angle);
    if(furi_hal_pwm_is_running(FuriHalPwmOutputIdTim1PA7)) {
        furi_hal_pwm_set_params(FuriHalPwmOutputIdTim1PA7, FLIPBUZZER_SERVO_FREQ, duty);
    } else {
        furi_hal_pwm_start(FuriHalPwmOutputIdTim1PA7, FLIPBUZZER_SERVO_FREQ, duty);
    }

    app->generator_playing = false;
    app->servo_angle = angle;
    app->servo_active = furi_hal_pwm_is_running(FuriHalPwmOutputIdTim1PA7);
}

static void flipbuzzer_play_sequence(
    FlipBuzzerApp* app,
    const FlipBuzzerToneStep* steps,
    size_t count,
    const char* status_text) {
    furi_assert(app);
    furi_assert(steps);

    flipbuzzer_pwm_stop(app);
    if(status_text) {
        flipbuzzer_set_status(app, status_text);
        flipbuzzer_main_view_update(app);
    }

    for(size_t i = 0; i < count; i++) {
        if(steps[i].frequency == 0 || steps[i].duty == 0) {
            flipbuzzer_pwm_stop(app);
        } else {
            flipbuzzer_pwm_start(app, steps[i].frequency, steps[i].duty);
        }

        furi_delay_ms(steps[i].duration_ms);
    }

    flipbuzzer_pwm_stop(app);
}

static void flipbuzzer_file_playback_stop(FlipBuzzerApp* app) {
    furi_assert(app);
    flipbuzzer_pwm_stop(app);
    flipbuzzer_led_blink_stop(app);
    app->file_playback_active = false;
    app->file_playback_paused = false;
    app->file_playback_pending_start = false;
}

static void flipbuzzer_file_playback_apply_step(FlipBuzzerApp* app) {
    furi_assert(app);

    if(!app->file_playback_active || app->file_playback_paused ||
       app->file_step_index >= app->file_step_count) {
        flipbuzzer_pwm_stop(app);
        return;
    }

    const FlipBuzzerToneStep* step = &app->file_steps[app->file_step_index];
    if(step->frequency == 0 || step->duty == 0) {
        flipbuzzer_pwm_stop(app);
    } else {
        flipbuzzer_pwm_start(app, step->frequency, step->duty);
    }
}

static bool flipbuzzer_parse_u32_token(const char* token, uint32_t* value) {
    if(!token || !value || !*token) return false;

    char* end = NULL;
    unsigned long parsed = strtoul(token, &end, 10);
    if(end == token || *end != '\0') return false;

    *value = (uint32_t)parsed;
    return true;
}

static char* flipbuzzer_next_line(char** cursor) {
    if(!cursor || !*cursor || !**cursor) return NULL;

    char* line = *cursor;
    char* next = line;

    while(*next && *next != '\r' && *next != '\n') next++;

    if(*next) {
        *next = '\0';
        next++;
        if(*next == '\n' || *next == '\r') {
            next++;
        }
    }

    *cursor = next;
    return line;
}

static size_t flipbuzzer_split_tokens(char* line, char** tokens, size_t max_tokens) {
    size_t count = 0;
    char* cursor = line;

    while(*cursor && count < max_tokens) {
        while(*cursor && strchr(" \t,;", *cursor)) cursor++;
        if(!*cursor) break;

        tokens[count++] = cursor;

        while(*cursor && !strchr(" \t,;", *cursor)) cursor++;
        if(!*cursor) break;

        *cursor = '\0';
        cursor++;
    }

    return count;
}

static bool flipbuzzer_load_sequence_from_file(
    FlipBuzzerApp* app,
    const char* path,
    FlipBuzzerToneStep* out_steps,
    size_t* out_count) {
    furi_assert(app);
    furi_assert(path);
    furi_assert(out_steps);
    furi_assert(out_count);

    bool success = false;
    File* file = storage_file_alloc(app->storage);
    if(!file) return false;

    do {
        if(!storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) break;

        uint64_t file_size = storage_file_size(file);
        if(file_size == 0 || file_size > 4096) break;

        char* buffer = malloc((size_t)file_size + 1);
        if(!buffer) break;

        size_t bytes_read = storage_file_read(file, buffer, (size_t)file_size);
        buffer[bytes_read] = '\0';

        size_t parsed_count = 0;
        char* cursor = buffer;
        char* line = flipbuzzer_next_line(&cursor);

        while(line && parsed_count < FLIPBUZZER_MAX_NOTES) {
            while(isspace((unsigned char)*line)) line++;

            if(*line && *line != '#') {
                char* tokens[3] = {0};
                size_t token_count = flipbuzzer_split_tokens(line, tokens, COUNT_OF(tokens));

                if(token_count >= 2) {
                    uint32_t frequency = 0;
                    uint32_t duration_ms = 0;
                    uint32_t duty = FLIPBUZZER_DEFAULT_DUTY;

                    if(flipbuzzer_parse_u32_token(tokens[0], &frequency) &&
                       flipbuzzer_parse_u32_token(tokens[1], &duration_ms)) {
                        if(token_count >= 3) {
                            if(!flipbuzzer_parse_u32_token(tokens[2], &duty)) {
                                duration_ms = 0;
                            }
                        }

                        if(duration_ms > 0) {
                            if(frequency > FLIPBUZZER_MAX_FREQ) frequency = FLIPBUZZER_MAX_FREQ;
                            if(duty < FLIPBUZZER_MIN_DUTY) duty = FLIPBUZZER_MIN_DUTY;
                            if(duty > FLIPBUZZER_MAX_DUTY) duty = FLIPBUZZER_MAX_DUTY;

                            out_steps[parsed_count].frequency = (uint16_t)frequency;
                            out_steps[parsed_count].duration_ms = (uint16_t)duration_ms;
                            out_steps[parsed_count].duty = (uint8_t)duty;
                            parsed_count++;
                        }
                    }
                }
            }

            line = flipbuzzer_next_line(&cursor);
        }

        free(buffer);
        *out_count = parsed_count;
        success = parsed_count > 0;
    } while(false);

    storage_file_close(file);
    storage_file_free(file);
    return success;
}

static const char* flipbuzzer_morse_for_char(char c) {
    switch(toupper((unsigned char)c)) {
    case 'A': return ".-";
    case 'B': return "-...";
    case 'C': return "-.-.";
    case 'D': return "-..";
    case 'E': return ".";
    case 'F': return "..-.";
    case 'G': return "--.";
    case 'H': return "....";
    case 'I': return "..";
    case 'J': return ".---";
    case 'K': return "-.-";
    case 'L': return ".-..";
    case 'M': return "--";
    case 'N': return "-.";
    case 'O': return "---";
    case 'P': return ".--.";
    case 'Q': return "--.-";
    case 'R': return ".-.";
    case 'S': return "...";
    case 'T': return "-";
    case 'U': return "..-";
    case 'V': return "...-";
    case 'W': return ".--";
    case 'X': return "-..-";
    case 'Y': return "-.--";
    case 'Z': return "--..";
    case '0': return "-----";
    case '1': return ".----";
    case '2': return "..---";
    case '3': return "...--";
    case '4': return "....-";
    case '5': return ".....";
    case '6': return "-....";
    case '7': return "--...";
    case '8': return "---..";
    case '9': return "----.";
    case '.': return ".-.-.-";
    case ',': return "--..--";
    case '?': return "..--..";
    case '!': return "-.-.--";
    case '/': return "-..-.";
    case '-': return "-....-";
    case '(': return "-.--.";
    case ')': return "-.--.-";
    case ':': return "---...";
    case ';': return "-.-.-.";
    case '=': return "-...-";
    case '+': return ".-.-.";
    case '@': return ".--.-.";
    case '\'': return ".----.";
    case '"': return ".-..-.";
    default: return NULL;
    }
}

static void flipbuzzer_play_morse_text(FlipBuzzerApp* app) {
    static const uint32_t dot_ms = 90;
    static const uint32_t dash_ms = dot_ms * 3;
    static const uint32_t symbol_gap_ms = dot_ms;
    static const uint32_t letter_gap_ms = dot_ms * 3;
    static const uint32_t word_gap_ms = dot_ms * 7;

    if(app->morse_text[0] == '\0') {
        flipbuzzer_set_status(app, "Enter a Morse message");
        return;
    }

    snprintf(
        app->status,
        sizeof(app->status),
        "Sending Morse on %s",
        flipbuzzer_get_output_label(app));
    flipbuzzer_main_view_update(app);

    for(size_t i = 0; app->morse_text[i] != '\0'; i++) {
        char c = app->morse_text[i];
        if(c == ' ') {
            flipbuzzer_pwm_stop(app);
            furi_delay_ms(word_gap_ms);
            continue;
        }

        const char* code = flipbuzzer_morse_for_char(c);
        if(!code) continue;

        for(size_t j = 0; code[j] != '\0'; j++) {
            uint32_t tone_ms = (code[j] == '-') ? dash_ms : dot_ms;
            flipbuzzer_pwm_start(app, 1000, app->generator_duty);
            furi_delay_ms(tone_ms);
            flipbuzzer_pwm_stop(app);
            if(code[j + 1] != '\0') {
                furi_delay_ms(symbol_gap_ms);
            }
        }

        if(app->morse_text[i + 1] != '\0' && app->morse_text[i + 1] != ' ') {
            furi_delay_ms(letter_gap_ms);
        }
    }

    flipbuzzer_set_status(app, "Morse message sent");
}

static uint32_t flipbuzzer_text_input_back_callback(void* context) {
    UNUSED(context);
    return FlipBuzzerViewMain;
}

static void flipbuzzer_morse_text_ready_callback(void* context) {
    FlipBuzzerApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, FlipBuzzerCustomEventMorseTextReady);
}

static bool flipbuzzer_custom_event_callback(void* context, uint32_t event) {
    FlipBuzzerApp* app = context;
    if(event == FlipBuzzerCustomEventMorseTextReady) {
        app->screen = FlipBuzzerScreenMorseCode;
        flipbuzzer_set_status(app, "Morse text updated");
        view_dispatcher_switch_to_view(app->view_dispatcher, FlipBuzzerViewMain);
        return true;
    }
    return false;
}

static void flipbuzzer_file_playback_start(FlipBuzzerApp* app, const char* path) {
    furi_assert(app);
    furi_assert(path);

    flipbuzzer_file_playback_stop(app);

    size_t count = 0;
    if(!flipbuzzer_load_sequence_from_file(app, path, app->file_steps, &count)) {
        flipbuzzer_set_status(app, "Invalid sound file");
        return;
    }

    FuriString* full_path = furi_string_alloc_set(path);
    FuriString* file_name = furi_string_alloc();
    path_extract_filename(full_path, file_name, true);
    strlcpy(
        app->current_file_name, furi_string_get_cstr(file_name), sizeof(app->current_file_name));
    furi_string_free(file_name);
    furi_string_free(full_path);

    app->file_step_count = count;
    app->file_step_index = 0;
    app->file_step_elapsed_ms = 0;
    app->file_elapsed_ms = 0;
    app->file_total_ms = 0;
    for(size_t i = 0; i < count; i++) {
        app->file_total_ms += app->file_steps[i].duration_ms;
    }

    app->file_playback_active = true;
    app->file_playback_paused = false;
    app->file_playback_pending_start = true;
    app->screen = FlipBuzzerScreenFilePlayback;
    flipbuzzer_set_status(app, "File playback started");
    flipbuzzer_main_view_update(app);
}

static void flipbuzzer_browse_and_play_file(FlipBuzzerApp* app) {
    furi_assert(app);

    storage_simply_mkdir(app->storage, FLIPBUZZER_SOUND_DIR);

    FuriString* path = furi_string_alloc_set(FLIPBUZZER_SOUND_DIR);
    DialogsFileBrowserOptions browser_options;
    dialog_file_browser_set_basic_options(&browser_options, FLIPBUZZER_SOUND_EXT, NULL);
    browser_options.base_path = FLIPBUZZER_SOUND_DIR;
    browser_options.hide_ext = true;
    browser_options.skip_assets = true;

    if(dialog_file_browser_show(app->dialogs, path, path, &browser_options)) {
        flipbuzzer_file_playback_start(app, furi_string_get_cstr(path));
    } else {
        flipbuzzer_set_status(app, "File selection canceled");
    }

    furi_string_free(path);
}

static void flipbuzzer_draw_main_menu(Canvas* canvas, const FlipBuzzerApp* app) {
    const uint8_t visible_items = 5;
    uint8_t start_index = 0;

    if(FlipBuzzerMainMenuCount > visible_items) {
        if(app->main_menu_index >= visible_items - 1) {
            start_index = app->main_menu_index - (visible_items - 2);
        }
        if(start_index > FlipBuzzerMainMenuCount - visible_items) {
            start_index = FlipBuzzerMainMenuCount - visible_items;
        }
    }

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, FLIPBUZZER_APP_NAME);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(
        canvas, 126, 1, AlignRight, AlignTop, flipbuzzer_get_output_label(app));
    canvas_draw_line(canvas, 0, 13, 127, 13);

    for(uint8_t row = 0; row < visible_items; row++) {
        uint8_t i = start_index + row;
        if(i >= FlipBuzzerMainMenuCount) break;

        uint8_t y = 22 + (row * 9);
        if(i == app->main_menu_index) {
            canvas_draw_box(canvas, 0, y - 8, 108, 10);
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_str(canvas, 4, y, flipbuzzer_main_menu_items[i]);
            canvas_set_color(canvas, ColorBlack);
        } else {
            canvas_draw_str(canvas, 8, y, flipbuzzer_main_menu_items[i]);
        }
    }

    if(start_index > 0) {
        canvas_draw_str(canvas, 116, 23, "^");
    }
    if((start_index + visible_items) < FlipBuzzerMainMenuCount) {
        canvas_draw_str(canvas, 116, 59, "v");
    }
}

static void flipbuzzer_draw_about(Canvas* canvas) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, "About");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 27, "Generator lets you");
    canvas_draw_str(canvas, 2, 38, "choose Ext/Int/Both.");
    canvas_draw_str(canvas, 2, 50, "ConsultingJoe.com");
    canvas_draw_line(canvas, 0, 52, 127, 52);
    canvas_draw_str(canvas, 2, 61, "Back Menu");
}

static void flipbuzzer_draw_output_mode(Canvas* canvas, const FlipBuzzerApp* app) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 11, "Output Mode");
    canvas_set_font(canvas, FontSecondary);

    for(uint8_t i = 0; i < FlipBuzzerOutputCount; i++) {
        uint8_t y = 24 + (i * 12);
        if(i == app->output_mode) {
            canvas_draw_box(canvas, 0, y - 10, 128, 12);
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_str(canvas, 4, y, flipbuzzer_output_mode_labels[i]);
            canvas_set_color(canvas, ColorBlack);
        } else {
            canvas_draw_str(canvas, 8, y, flipbuzzer_output_mode_labels[i]);
        }
    }

    canvas_draw_line(canvas, 0, 52, 127, 52);
    canvas_draw_str(canvas, 2, 61, "OK Select");
    canvas_draw_str(canvas, 78, 61, "Back Menu");
}

static void flipbuzzer_draw_generator(Canvas* canvas, const FlipBuzzerApp* app) {
    char line[40];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 11, "Frequency Generator");
    canvas_set_font(canvas, FontSecondary);

    snprintf(line, sizeof(line), "Freq: %lu Hz", (unsigned long)app->generator_frequency);
    canvas_draw_str(canvas, 2, 25, line);

    snprintf(line, sizeof(line), "Duty: %u%%", app->generator_duty);
    canvas_draw_str(canvas, 2, 37, line);

    snprintf(line, sizeof(line), "Out: %s", flipbuzzer_get_output_label(app));
    canvas_draw_str(canvas, 2, 49, line);

    canvas_draw_line(canvas, 0, 52, 127, 52);
    canvas_draw_str(canvas, 2, 61, "Hz / Duty");
    if(app->generator_playing) {
        canvas_draw_str_aligned(canvas, 126, 54, AlignRight, AlignTop, "OK Stop");
    } else {
        canvas_draw_str_aligned(canvas, 126, 54, AlignRight, AlignTop, "OK Play");
    }
}

static void flipbuzzer_draw_servo_control(Canvas* canvas, const FlipBuzzerApp* app) {
    char line[40];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 11, "Servo Control");
    canvas_set_font(canvas, FontSecondary);

    snprintf(line, sizeof(line), "Pin: A7");
    canvas_draw_str(canvas, 2, 25, line);

    snprintf(line, sizeof(line), "Angle: %u deg", app->servo_angle);
    canvas_draw_str(canvas, 2, 37, line);

    canvas_draw_str(canvas, 2, 49, app->servo_active ? "State: Holding" : "State: Idle");

    canvas_draw_line(canvas, 0, 52, 127, 52);
    canvas_draw_str(canvas, 2, 61, "L/R Angle");
    if(app->servo_active) {
        canvas_draw_str_aligned(canvas, 126, 54, AlignRight, AlignTop, "OK Stop");
    } else {
        canvas_draw_str_aligned(canvas, 126, 54, AlignRight, AlignTop, "OK Hold");
    }
}

static void flipbuzzer_draw_saved_sounds(Canvas* canvas, const FlipBuzzerApp* app) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 11, "Saved Sounds");
    canvas_set_font(canvas, FontSecondary);

    for(uint8_t i = 0; i < FlipBuzzerSavedSoundCount; i++) {
        uint8_t y = 24 + (i * 12);
        if(i == app->saved_sound_index) {
            canvas_draw_box(canvas, 0, y - 10, 128, 12);
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_str(canvas, 4, y, flipbuzzer_saved_sound_items[i]);
            canvas_set_color(canvas, ColorBlack);
        } else {
            canvas_draw_str(canvas, 8, y, flipbuzzer_saved_sound_items[i]);
        }
    }

    canvas_draw_line(canvas, 0, 52, 127, 52);
    canvas_draw_str(canvas, 2, 61, "OK Play");
    canvas_draw_str(canvas, 78, 61, "Back Menu");
}

static void flipbuzzer_draw_file_playback(Canvas* canvas, const FlipBuzzerApp* app) {
    char line[40];
    uint8_t progress_width = 0;
    uint32_t progress_percent = 0;
    uint32_t current_step = 0;

    if(app->file_total_ms > 0) {
        progress_percent = (app->file_elapsed_ms * 100U) / app->file_total_ms;
        progress_width = (uint8_t)((app->file_elapsed_ms * 122U) / app->file_total_ms);
        if(progress_width > 122) progress_width = 122;
    }

    if(app->file_step_count > 0) {
        current_step = app->file_step_index + (app->file_playback_active ? 1U : 0U);
        if(current_step > app->file_step_count) current_step = app->file_step_count;
    }

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 11, "File Playback");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(
        canvas, 2, 25, app->current_file_name[0] ? app->current_file_name : "<sound file>");

    snprintf(
        line,
        sizeof(line),
        "%s %lu/%lu",
        app->file_playback_paused ? "Paused" : "Playing",
        (unsigned long)current_step,
        (unsigned long)app->file_step_count);
    canvas_draw_str(canvas, 2, 37, line);

    snprintf(line, sizeof(line), "%lu%%", (unsigned long)progress_percent);
    canvas_draw_str_aligned(canvas, 126, 33, AlignRight, AlignTop, line);

    canvas_draw_frame(canvas, 2, 41, 124, 8);
    if(progress_width > 0) {
        canvas_draw_box(canvas, 3, 42, progress_width, 6);
    }

    canvas_draw_line(canvas, 0, 52, 127, 52);
    canvas_draw_str(canvas, 2, 61, app->file_playback_paused ? "OK Resume" : "OK Pause");
    canvas_draw_str_aligned(canvas, 126, 54, AlignRight, AlignTop, "Back Stop");
}

static void flipbuzzer_draw_callback(Canvas* canvas, void* model) {
    FlipBuzzerMainViewModel* main_model = model;
    FlipBuzzerApp* app = main_model->app;
    furi_assert(app);

    canvas_clear(canvas);

    switch(app->screen) {
    case FlipBuzzerScreenMainMenu:
        flipbuzzer_draw_main_menu(canvas, app);
        break;
    case FlipBuzzerScreenOutputMode:
        flipbuzzer_draw_output_mode(canvas, app);
        break;
    case FlipBuzzerScreenFrequencyGenerator:
        flipbuzzer_draw_generator(canvas, app);
        break;
    case FlipBuzzerScreenServoControl:
        flipbuzzer_draw_servo_control(canvas, app);
        break;
    case FlipBuzzerScreenSavedSoundsMenu:
        flipbuzzer_draw_saved_sounds(canvas, app);
        break;
    case FlipBuzzerScreenFilePlayback:
        flipbuzzer_draw_file_playback(canvas, app);
        break;
    case FlipBuzzerScreenMorseCode: {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 2, 11, "Morse Code");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 25, "OK Edit message");
        canvas_draw_str(canvas, 2, 37, "Long OK Send");
        canvas_draw_str(canvas, 2, 49, app->morse_text[0] ? app->morse_text : "<empty>");
        canvas_draw_line(canvas, 0, 52, 127, 52);
        canvas_draw_str(canvas, 2, 61, "Back Menu");
        break;
    }
    case FlipBuzzerScreenAbout:
        flipbuzzer_draw_about(canvas);
        break;
    }

}

static bool flipbuzzer_input_callback(InputEvent* event, void* context) {
    FlipBuzzerApp* app = context;
    furi_assert(app);

    if(app->screen == FlipBuzzerScreenMainMenu) {
        flipbuzzer_handle_main_menu(app, event);
    } else if(app->screen == FlipBuzzerScreenOutputMode) {
        if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
            if(event->key == InputKeyUp) {
                if(app->output_mode == 0) {
                    app->output_mode = FlipBuzzerOutputCount - 1;
                } else {
                    app->output_mode--;
                }
            } else if(event->key == InputKeyDown) {
                app->output_mode = (app->output_mode + 1) % FlipBuzzerOutputCount;
            } else if(event->key == InputKeyOk) {
                snprintf(
                    app->status,
                    sizeof(app->status),
                    "Output: %s",
                    flipbuzzer_get_output_label(app));
                app->screen = FlipBuzzerScreenMainMenu;
            } else if(event->key == InputKeyBack) {
                app->screen = FlipBuzzerScreenMainMenu;
                flipbuzzer_set_status(app, "Main menu");
            }
        }
    } else if(app->screen == FlipBuzzerScreenFrequencyGenerator) {
        flipbuzzer_handle_generator(app, event);
    } else if(app->screen == FlipBuzzerScreenServoControl) {
        flipbuzzer_handle_servo_control(app, event);
    } else if(app->screen == FlipBuzzerScreenSavedSoundsMenu) {
        flipbuzzer_handle_saved_sounds(app, event);
    } else if(app->screen == FlipBuzzerScreenFilePlayback) {
        flipbuzzer_handle_file_playback(app, event);
    } else if(app->screen == FlipBuzzerScreenMorseCode) {
        if((event->type == InputTypeShort || event->type == InputTypeRepeat) &&
           event->key == InputKeyOk) {
            text_input_reset(app->text_input);
            text_input_set_header_text(app->text_input, "Morse message");
            text_input_set_minimum_length(app->text_input, 1);
            text_input_set_result_callback(
                app->text_input,
                flipbuzzer_morse_text_ready_callback,
                app,
                app->morse_text,
                sizeof(app->morse_text),
                false);
            view_dispatcher_switch_to_view(app->view_dispatcher, FlipBuzzerViewTextInput);
        } else if(event->type == InputTypeLong && event->key == InputKeyOk) {
            flipbuzzer_play_morse_text(app);
        } else if((event->type == InputTypeShort || event->type == InputTypeRepeat) &&
                  event->key == InputKeyBack) {
            app->screen = FlipBuzzerScreenMainMenu;
            flipbuzzer_set_status(app, "Main menu");
        }
    } else if(app->screen == FlipBuzzerScreenAbout) {
        if((event->type == InputTypeShort || event->type == InputTypeRepeat) &&
           event->key == InputKeyBack) {
            app->screen = FlipBuzzerScreenMainMenu;
            flipbuzzer_set_status(app, "Main menu");
        }
    }

    flipbuzzer_main_view_update(app);
    if(!app->running) {
        view_dispatcher_stop(app->view_dispatcher);
    }
    return true;
}

static void flipbuzzer_handle_main_menu(FlipBuzzerApp* app, const InputEvent* event) {
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return;

    if(event->key == InputKeyUp) {
        if(app->main_menu_index == 0) {
            app->main_menu_index = FlipBuzzerMainMenuCount - 1;
        } else {
            app->main_menu_index--;
        }
    } else if(event->key == InputKeyDown) {
        app->main_menu_index = (app->main_menu_index + 1) % FlipBuzzerMainMenuCount;
    } else if(event->key == InputKeyOk) {
        switch(app->main_menu_index) {
        case FlipBuzzerMainMenuOutputMode:
            app->screen = FlipBuzzerScreenOutputMode;
            flipbuzzer_set_status(app, "Choose output mode");
            break;
        case FlipBuzzerMainMenuFrequencyGenerator:
            app->screen = FlipBuzzerScreenFrequencyGenerator;
            flipbuzzer_set_status(app, "Adjust freq, duty, output");
            break;
        case FlipBuzzerMainMenuServoControl:
            app->screen = FlipBuzzerScreenServoControl;
            flipbuzzer_set_status(app, "Servo control on A7");
            break;
        case FlipBuzzerMainMenuSavedSounds:
            app->screen = FlipBuzzerScreenSavedSoundsMenu;
            flipbuzzer_set_status(app, "Built-in and file sounds");
            break;
        case FlipBuzzerMainMenuMorseCode:
            app->screen = FlipBuzzerScreenMorseCode;
            flipbuzzer_set_status(app, "Type and send Morse");
            break;
        case FlipBuzzerMainMenuAbout:
            app->screen = FlipBuzzerScreenAbout;
            flipbuzzer_set_status(app, "");
            break;
        default:
            break;
        }
    } else if(event->key == InputKeyBack) {
        app->running = false;
    }
}

static void flipbuzzer_handle_generator(FlipBuzzerApp* app, const InputEvent* event) {
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) {
        if(event->type == InputTypeLong && event->key == InputKeyOk) {
            app->generator_frequency = FLIPBUZZER_DEFAULT_FREQ;
            app->generator_duty = FLIPBUZZER_DEFAULT_DUTY;
            if(app->generator_playing) {
                flipbuzzer_pwm_start(app, app->generator_frequency, app->generator_duty);
            }
            flipbuzzer_set_status(app, "Generator reset");
        }
        return;
    }

    if(event->key == InputKeyLeft) {
        uint32_t step = (app->generator_frequency >= 1000) ? 100 : 10;
        app->generator_frequency = flipbuzzer_clamp_frequency(app->generator_frequency - step);
        if(app->generator_playing) {
            flipbuzzer_pwm_start(app, app->generator_frequency, app->generator_duty);
        }
    } else if(event->key == InputKeyRight) {
        uint32_t step = (app->generator_frequency >= 1000) ? 100 : 10;
        app->generator_frequency = flipbuzzer_clamp_frequency(app->generator_frequency + step);
        if(app->generator_playing) {
            flipbuzzer_pwm_start(app, app->generator_frequency, app->generator_duty);
        }
    } else if(event->key == InputKeyUp) {
        app->generator_duty = flipbuzzer_clamp_duty(app->generator_duty + 1);
        if(app->generator_playing) {
            flipbuzzer_pwm_start(app, app->generator_frequency, app->generator_duty);
        }
    } else if(event->key == InputKeyDown) {
        app->generator_duty = flipbuzzer_clamp_duty(app->generator_duty - 1);
        if(app->generator_playing) {
            flipbuzzer_pwm_start(app, app->generator_frequency, app->generator_duty);
        }
    } else if(event->key == InputKeyOk) {
        if(app->generator_playing) {
            flipbuzzer_pwm_stop(app);
            flipbuzzer_set_status(app, "Generator stopped");
        } else {
            flipbuzzer_pwm_start(app, app->generator_frequency, app->generator_duty);
            flipbuzzer_set_status(app, "Generator playing");
        }
    } else if(event->key == InputKeyBack) {
        flipbuzzer_pwm_stop(app);
        app->screen = FlipBuzzerScreenMainMenu;
        flipbuzzer_set_status(app, "Main menu");
    }
}

static void flipbuzzer_handle_servo_control(FlipBuzzerApp* app, const InputEvent* event) {
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) {
        if(event->type == InputTypeLong && event->key == InputKeyOk) {
            app->servo_angle = FLIPBUZZER_SERVO_DEFAULT_ANGLE;
            if(app->servo_active) {
                flipbuzzer_servo_start(app, app->servo_angle);
            }
            flipbuzzer_set_status(app, "Servo centered");
        }
        return;
    }

    if(event->key == InputKeyLeft) {
        if(app->servo_angle >= 5U) {
            app->servo_angle -= 5U;
        } else {
            app->servo_angle = FLIPBUZZER_SERVO_MIN_ANGLE;
        }
        if(app->servo_active) {
            flipbuzzer_servo_start(app, app->servo_angle);
        }
    } else if(event->key == InputKeyRight) {
        uint16_t next_angle = app->servo_angle + 5U;
        app->servo_angle = (next_angle > FLIPBUZZER_SERVO_MAX_ANGLE) ?
                               FLIPBUZZER_SERVO_MAX_ANGLE :
                               (uint8_t)next_angle;
        if(app->servo_active) {
            flipbuzzer_servo_start(app, app->servo_angle);
        }
    } else if(event->key == InputKeyOk) {
        if(app->servo_active) {
            flipbuzzer_pwm_stop(app);
            flipbuzzer_set_status(app, "Servo stopped");
        } else {
            flipbuzzer_servo_start(app, app->servo_angle);
            flipbuzzer_set_status(app, "Servo holding on A7");
        }
    } else if(event->key == InputKeyBack) {
        flipbuzzer_pwm_stop(app);
        app->screen = FlipBuzzerScreenMainMenu;
        flipbuzzer_set_status(app, "Main menu");
    }
}

static void flipbuzzer_handle_saved_sounds(FlipBuzzerApp* app, const InputEvent* event) {
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return;

    if(event->key == InputKeyUp) {
        if(app->saved_sound_index == 0) {
            app->saved_sound_index = FlipBuzzerSavedSoundCount - 1;
        } else {
            app->saved_sound_index--;
        }
    } else if(event->key == InputKeyDown) {
        app->saved_sound_index = (app->saved_sound_index + 1) % FlipBuzzerSavedSoundCount;
    } else if(event->key == InputKeyOk) {
        switch(app->saved_sound_index) {
        case FlipBuzzerSavedSoundBuiltinStartup:
            flipbuzzer_play_sequence(
                app,
                flipbuzzer_startup_sound,
                COUNT_OF(flipbuzzer_startup_sound),
                "Playing startup chime");
            break;
        case FlipBuzzerSavedSoundBuiltinAlert:
            flipbuzzer_play_sequence(
                app,
                flipbuzzer_alert_sound,
                COUNT_OF(flipbuzzer_alert_sound),
                "Playing alert beep");
            break;
        case FlipBuzzerSavedSoundBrowseFiles:
            flipbuzzer_browse_and_play_file(app);
            break;
        default:
            break;
        }
    } else if(event->key == InputKeyBack) {
        app->screen = FlipBuzzerScreenMainMenu;
        flipbuzzer_set_status(app, "Main menu");
    }
}

static void flipbuzzer_handle_file_playback(FlipBuzzerApp* app, const InputEvent* event) {
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return;

    if(event->key == InputKeyOk) {
        if(!app->file_playback_active) {
            return;
        } else if(app->file_playback_paused) {
            app->file_playback_paused = false;
            flipbuzzer_led_blink_start(app);
            flipbuzzer_file_playback_apply_step(app);
            flipbuzzer_set_status(app, "Playback resumed");
        } else {
            app->file_playback_paused = true;
            flipbuzzer_pwm_stop(app);
            flipbuzzer_led_blink_stop(app);
            flipbuzzer_set_status(app, "Playback paused");
        }
    } else if(event->key == InputKeyBack) {
        flipbuzzer_file_playback_stop(app);
        app->screen = FlipBuzzerScreenSavedSoundsMenu;
        flipbuzzer_set_status(app, "Playback stopped");
    }
}

static void flipbuzzer_tick_callback(void* context) {
    FlipBuzzerApp* app = context;
    furi_assert(app);

    if(!app->file_playback_active || app->file_playback_paused) return;
    if(app->file_playback_pending_start) {
        app->file_playback_pending_start = false;
        flipbuzzer_led_blink_start(app);
        flipbuzzer_file_playback_apply_step(app);
        flipbuzzer_main_view_update(app);
        return;
    }
    if(app->file_step_index >= app->file_step_count) return;

    app->file_step_elapsed_ms += FLIPBUZZER_PLAYBACK_TICK_MS;
    app->file_elapsed_ms += FLIPBUZZER_PLAYBACK_TICK_MS;
    if(app->file_elapsed_ms > app->file_total_ms) {
        app->file_elapsed_ms = app->file_total_ms;
    }

    while(app->file_step_index < app->file_step_count &&
          app->file_step_elapsed_ms >= app->file_steps[app->file_step_index].duration_ms) {
        app->file_step_elapsed_ms -= app->file_steps[app->file_step_index].duration_ms;
        app->file_step_index++;
        if(app->file_step_index >= app->file_step_count) {
            char finished_name[FLIPBUZZER_FILE_NAME_LEN];
            strlcpy(finished_name, app->current_file_name, sizeof(finished_name));
            flipbuzzer_file_playback_stop(app);
            app->screen = FlipBuzzerScreenSavedSoundsMenu;
            snprintf(app->status, sizeof(app->status), "Finished %s", finished_name);
            break;
        }
        flipbuzzer_file_playback_apply_step(app);
    }

    flipbuzzer_main_view_update(app);
}

static FlipBuzzerApp* flipbuzzer_alloc(void) {
    FlipBuzzerApp* app = malloc(sizeof(FlipBuzzerApp));
    furi_assert(app);

    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->notification = furi_record_open(RECORD_NOTIFICATION);
    app->view_dispatcher = view_dispatcher_alloc();
    app->main_view = view_alloc();
    app->text_input = text_input_alloc();

    app->screen = FlipBuzzerScreenMainMenu;
    app->main_menu_index = 0;
    app->saved_sound_index = 0;
    app->generator_frequency = FLIPBUZZER_DEFAULT_FREQ;
    app->generator_duty = FLIPBUZZER_DEFAULT_DUTY;
    app->servo_angle = FLIPBUZZER_SERVO_DEFAULT_ANGLE;
    app->output_mode = FlipBuzzerOutputBoth;
    app->generator_playing = false;
    app->servo_active = false;
    app->file_playback_active = false;
    app->file_playback_paused = false;
    app->file_playback_pending_start = false;
    app->speaker_acquired = false;
    app->running = true;
    flipbuzzer_set_status(app, "Startup chime on launch");
    strlcpy(app->morse_text, "SOS", sizeof(app->morse_text));
    app->current_file_name[0] = '\0';

    view_set_context(app->main_view, app);
    view_allocate_model(app->main_view, ViewModelTypeLockFree, sizeof(FlipBuzzerMainViewModel));
    FlipBuzzerMainViewModel* model = view_get_model(app->main_view);
    model->app = app;
    view_commit_model(app->main_view, false);
    view_set_draw_callback(app->main_view, flipbuzzer_draw_callback);
    view_set_input_callback(app->main_view, flipbuzzer_input_callback);

    view_set_previous_callback(
        text_input_get_view(app->text_input), flipbuzzer_text_input_back_callback);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, flipbuzzer_custom_event_callback);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, flipbuzzer_tick_callback, FLIPBUZZER_PLAYBACK_TICK_MS);
    view_dispatcher_add_view(app->view_dispatcher, FlipBuzzerViewMain, app->main_view);
    view_dispatcher_add_view(
        app->view_dispatcher, FlipBuzzerViewTextInput, text_input_get_view(app->text_input));
    view_dispatcher_attach_to_gui(
        app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipBuzzerViewMain);

    return app;
}

static void flipbuzzer_free(FlipBuzzerApp* app) {
    furi_assert(app);

    flipbuzzer_file_playback_stop(app);
    view_dispatcher_remove_view(app->view_dispatcher, FlipBuzzerViewTextInput);
    view_dispatcher_remove_view(app->view_dispatcher, FlipBuzzerViewMain);
    text_input_free(app->text_input);
    view_free(app->main_view);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t flipbuzzer_app(void* p) {
    UNUSED(p);

    FlipBuzzerApp* app = flipbuzzer_alloc();

    storage_simply_mkdir(app->storage, FLIPBUZZER_SOUND_DIR);
    flipbuzzer_play_sequence(
        app,
        flipbuzzer_startup_sound,
        COUNT_OF(flipbuzzer_startup_sound),
        "Playing startup chime");
    snprintf(app->status, sizeof(app->status), "Ready: %s", flipbuzzer_get_output_label(app));
    flipbuzzer_main_view_update(app);
    view_dispatcher_run(app->view_dispatcher);

    flipbuzzer_free(app);
    return 0;
}
