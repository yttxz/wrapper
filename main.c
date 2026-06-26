#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdarg.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <termios.h>

#include "import.h"
#include "cmdline.h"
#include "cJSON.h"
#ifndef MyRelease
#include "dobby.h"
#endif

#define TWO_FACTOR_CODE_LENGTH 6
#define TWO_FACTOR_DEFAULT_TIMEOUT_SECONDS 60
#define TWO_FACTOR_MAX_TIMEOUT_SECONDS 600
#define TWO_FACTOR_MAX_FILE_BYTES 64
#define TWO_FACTOR_POLL_SECONDS 3
#define PASSWORD_MAX_INPUT_BYTES 1024
#define MAX_DECRYPT_ADAM_ID_BYTES 32
#define MAX_DECRYPT_KEY_URI_BYTES 240
#define MAX_DECRYPT_SAMPLE_BYTES (16U * 1024U * 1024U)
#define MAX_M3U8_ADAM_ID_BYTES 32
#define MAX_ACCOUNT_REQUEST_BYTES 4096

static struct shared_ptr apInf;
static uint8_t leaseMgr[16];
static struct shared_ptr reqCtx;
struct gengetopt_args_info args_info;
char *amUsername, *amPassword;
static int amPasswordNeedsFree;
struct shared_ptr GUID;
int decryptCount = 1000;
int offlineFlag;
char *device_infos[9];

static char *g_storefront_id = NULL;
static char *g_dev_token = NULL;
static char *g_music_token = NULL;

#ifndef MyRelease
static int (*orig_debug_log_enabled)(void);
static int (*orig_android_log_print)(int prio, const char *tag, const char *fmt, ...);
static int (*orig_android_log_write)(int prio, const char *tag, const char *text);
static int (*orig_curl_easy_setopt)(void *curl, int option, ...);

int32_t CURLOPT_SSL_VERIFYPEER = 64;
int32_t CURLOPT_SSL_VERIFYHOST = 81;
int32_t CURLOPT_PINNEDPUBLICKEY = 10230;
int32_t CURLOPT_VERBOSE = 43;

static int verbose_runtime_logs_enabled(void) {
    const char *value = getenv("WRAPPER_VERBOSE_RUNTIME_LOGS");
    return value != NULL && value[0] != '\0' &&
           strcmp(value, "0") != 0 && strcmp(value, "false") != 0;
}

int curl_easy_setopt_hook(void *curl, int32_t option, ...) {
    va_list args;
    va_start(args, option);
    void* param = va_arg(args, void*);
    va_end(args);
 
    if (option == CURLOPT_SSL_VERIFYPEER || 
        option == CURLOPT_SSL_VERIFYHOST || 
        option == CURLOPT_PINNEDPUBLICKEY) {
        if (verbose_runtime_logs_enabled()) {
            fprintf(stderr, "[+] hooked curl_easy_setopt %d\n", option);
            orig_curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        }
        return orig_curl_easy_setopt(curl, option, 0L);
    }  else {
        return orig_curl_easy_setopt(curl, option, param);
    }
 
}

int android_log_print_hook(int prio, const char *tag, const char *fmt, ...) {
    char log_buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(log_buffer, sizeof(log_buffer), fmt, args);
    va_end(args);
    if (verbose_runtime_logs_enabled()) {
        fprintf(stderr, "[%s] %s\n", tag, log_buffer);
    }
    return 0;
}

int android_log_write_hook(int prio, const char *tag, const char *text) {
    if (verbose_runtime_logs_enabled()) {
        fprintf(stderr, "[%s] %s\n", tag, text);
    }
    return 0;
}

static uint8_t allDebug() { return verbose_runtime_logs_enabled() ? 1 : 0; }

void install_hooks() {
    DobbyHook((void*)_ZN13mediaplatform26DebugLogEnabledForPriorityENS_11LogPriorityE,
              (void*)allDebug,
              (void**)&orig_debug_log_enabled);

    DobbyHook((void*)__android_log_print, 
              (void*)android_log_print_hook, 
              (void**)&orig_android_log_print);

    DobbyHook((void*)__android_log_write, 
              (void*)android_log_write_hook, 
              (void**)&orig_android_log_write);

    DobbyHook((void*)curl_easy_setopt, 
              (void*)curl_easy_setopt_hook, 
              (void**)&orig_curl_easy_setopt);
}
#endif

int file_exists(const char *filename) {
  struct stat buffer;   
  return (stat (filename, &buffer) == 0);
}

static int parse_timeout_seconds(const char *env_name, const int default_value,
                                 const int max_value) {
    const char *value = getenv(env_name);
    if (value == NULL || value[0] == '\0') {
        return default_value;
    }

    errno = 0;
    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' ||
        parsed < 1 || parsed > max_value) {
        fprintf(stderr, "[!] ignoring invalid %s=%s; using %d seconds\n",
                env_name, value, default_value);
        return default_value;
    }

    return (int)parsed;
}

static int is_valid_2fa_code(const char *code) {
    if (code == NULL || strlen(code) != TWO_FACTOR_CODE_LENGTH) {
        return 0;
    }

    for (size_t i = 0; i < TWO_FACTOR_CODE_LENGTH; i++) {
        if (!isdigit((unsigned char)code[i])) {
            return 0;
        }
    }
    return 1;
}

static int read_2fa_code_file(const char *path, char code[TWO_FACTOR_CODE_LENGTH + 1]) {
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd == -1) {
        if (errno == ELOOP) {
            fprintf(stderr, "[!] Refusing symlinked 2FA code file: %s\n", path);
        } else if (errno == ENOENT) {
            return 0;
        } else {
            perror("open 2fa.txt");
        }
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        perror("fstat 2fa.txt");
        return -1;
    }

    if (!S_ISREG(st.st_mode)) {
        fprintf(stderr, "[!] Refusing 2FA code path that is not a regular file: %s\n",
                path);
        close(fd);
        return -1;
    }

    if ((st.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
        fprintf(stderr, "[!] Refusing 2FA code file accessible by group/other; use chmod 600 for %s\n",
                path);
        close(fd);
        return -1;
    }

    if (st.st_size == 0) {
        fprintf(stderr, "[!] 2FA code file is empty\n");
        close(fd);
        return 0;
    }

    if (st.st_size > TWO_FACTOR_MAX_FILE_BYTES) {
        fprintf(stderr, "[!] 2FA code file is too large\n");
        close(fd);
        return 0;
    }

    char raw[TWO_FACTOR_MAX_FILE_BYTES + 1] = {0};
    const size_t expected_bytes = (size_t)st.st_size;
    const ssize_t bytes_read = read(fd, raw, expected_bytes);
    if (bytes_read < 0) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        perror("read 2fa.txt");
        return -1;
    }
    close(fd);

    if ((size_t)bytes_read != expected_bytes) {
        fprintf(stderr, "[!] Failed to read complete 2FA code file\n");
        return -1;
    }

    for (ssize_t i = 0; i < bytes_read; i++) {
        if (raw[i] == '\0') {
            fprintf(stderr, "[!] Invalid 2FA code file; NUL bytes are not allowed\n");
            return 0;
        }
    }
    raw[bytes_read] = '\0';

    char *start = raw;
    while (isspace((unsigned char)*start)) {
        start++;
    }

    char *end = start + strlen(start);
    while (end > start && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';

    if (start[0] == '\0') {
        fprintf(stderr, "[!] 2FA code file is empty\n");
        return 0;
    }

    if (!is_valid_2fa_code(start)) {
        fprintf(stderr, "[!] Invalid 2FA code; expected exactly %d digits\n",
                TWO_FACTOR_CODE_LENGTH);
        return 0;
    }

    memcpy(code, start, TWO_FACTOR_CODE_LENGTH + 1);
    return 1;
}

static int read_hidden_line(const char *prompt, const char *non_tty_message,
                            char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0) {
        return 0;
    }

    if (!isatty(STDIN_FILENO)) {
        fprintf(stderr, "[!] %s\n", non_tty_message);
        return 0;
    }

    fprintf(stderr, "%s", prompt);
    fflush(stderr);

    int echo_disabled = 0;
    struct termios old_termios;
    if (tcgetattr(STDIN_FILENO, &old_termios) != 0) {
        perror("tcgetattr");
        fprintf(stderr, "\n");
        return 0;
    }

    struct termios new_termios = old_termios;
    new_termios.c_lflag &= ~(ECHO);
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_termios) != 0) {
        perror("tcsetattr");
        fprintf(stderr, "\n");
        return 0;
    }
    echo_disabled = 1;

    int ok = fgets(buffer, buffer_size, stdin) != NULL;
    int saved_errno = errno;

    if (echo_disabled && tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_termios) != 0) {
        perror("tcsetattr restore");
        ok = 0;
    }
    fprintf(stderr, "\n");

    errno = saved_errno;
    if (!ok) {
        return 0;
    }

    buffer[strcspn(buffer, "\r\n")] = '\0';
    return 1;
}

static void clear_sensitive_buffer(char *buffer, size_t size) {
    volatile unsigned char *ptr = (volatile unsigned char *)buffer;
    while (size-- > 0) {
        *ptr++ = 0;
    }
}

static void prompt_for_password_if_needed(void) {
    if (amPassword != NULL) {
        return;
    }

    char input[PASSWORD_MAX_INPUT_BYTES] = {0};
    if (!read_hidden_line("Password: ",
                          "stdin is not a TTY; rerun interactively or use legacy --login=username:password explicitly",
                          input, sizeof(input))) {
        clear_sensitive_buffer(input, sizeof(input));
        fprintf(stderr, "[!] Failed to read password\n");
        exit(EXIT_FAILURE);
    }

    if (input[0] == '\0') {
        clear_sensitive_buffer(input, sizeof(input));
        fprintf(stderr, "[!] password must not be empty\n");
        exit(EXIT_FAILURE);
    }

    amPassword = strdup(input);
    clear_sensitive_buffer(input, sizeof(input));
    if (amPassword == NULL) {
        perror("strdup password");
        exit(EXIT_FAILURE);
    }
    amPasswordNeedsFree = 1;
}

static void clear_prompted_password(void) {
    if (amPasswordNeedsFree && amPassword != NULL) {
        clear_sensitive_buffer(amPassword, strlen(amPassword));
        free(amPassword);
        amPassword = NULL;
        amPasswordNeedsFree = 0;
    }
}

static int format_base_path(char *buffer, const size_t buffer_size,
                            const char *suffix) {
    const int needed = snprintf(buffer, buffer_size, "%s%s",
                                args_info.base_dir_arg, suffix);
    if (needed < 0 || (size_t)needed >= buffer_size) {
        fprintf(stderr, "[!] path is too long: %s%s\n",
                args_info.base_dir_arg, suffix);
        return 0;
    }
    return 1;
}

int split_string_safe(const char *str, const char *delim, char **components, 
                      int max_components, char **out_copy_to_free) 
{
    *out_copy_to_free = NULL;

    char *copy = strdup(str);
    if (copy == NULL) {
        return -1; 
    }

    *out_copy_to_free = copy;

    int count = 0;
    char *saveptr;
    char *token;

    token = strtok_r(copy, delim, &saveptr);

    while (token != NULL) {
        if (count >= max_components) {
            return -2;
        }
        components[count] = token;
        count++;
        token = strtok_r(NULL, delim, &saveptr);
    }

    return count;
}

static void dialogHandler(long j, struct shared_ptr *protoDialogPtr,
                          struct shared_ptr *respHandler) {
    (void)respHandler;
    const char *const title = std_string_data(
        _ZNK17storeservicescore14ProtocolDialog5titleEv(protoDialogPtr->obj));
    fprintf(stderr, "[.] dialogHandler: {title: %s, message: %s}\n", title,
            std_string_data(_ZNK17storeservicescore14ProtocolDialog7messageEv(
                protoDialogPtr->obj)));

    unsigned char ptr[72];
    memset(ptr + 8, 0, 16);
    *(void **)(ptr) =
        &_ZTVNSt6__ndk120__shared_ptr_emplaceIN17storeservicescore22ProtocolDialogResponseENS_9allocatorIS2_EEEE +
        2;
    struct shared_ptr diagResp = {.obj = ptr + 24, .ctrl_blk = ptr};
    _ZN17storeservicescore22ProtocolDialogResponseC1Ev(diagResp.obj);

    struct std_vector *butVec =
        _ZNK17storeservicescore14ProtocolDialog7buttonsEv(protoDialogPtr->obj);
    if (strcmp("Sign In", title) == 0) {
        for (struct shared_ptr *b = butVec->begin; b != butVec->end; ++b) {
            if (strcmp("Use Existing Apple ID",
                       std_string_data(
                           _ZNK17storeservicescore14ProtocolButton5titleEv(
                               b->obj))) == 0) {
                _ZN17storeservicescore22ProtocolDialogResponse17setSelectedButtonERKNSt6__ndk110shared_ptrINS_14ProtocolButtonEEE(
                    diagResp.obj, b);
                break;
            }
        }
    } else {
        for (struct shared_ptr *b = butVec->begin; b != butVec->end; ++b) {
            fprintf(
                stderr, "[.] button %p: %s\n", b->obj,
                std_string_data(
                    _ZNK17storeservicescore14ProtocolButton5titleEv(b->obj)));
        }
    }
    _ZN20androidstoreservices28AndroidPresentationInterface28handleProtocolDialogResponseERKlRKNSt6__ndk110shared_ptrIN17storeservicescore22ProtocolDialogResponseEEE(
        apInf.obj, &j, &diagResp);
}

static void credentialHandler(struct shared_ptr *credReqHandler,
                              struct shared_ptr *credRespHandler) {
    (void)credRespHandler;
    const uint8_t need2FA =
        _ZNK17storeservicescore18CredentialsRequest28requiresHSA2VerificationCodeEv(
            credReqHandler->obj);
    fprintf(
        stderr, "[.] credentialHandler: {title: %s, message: %s, 2FA: %s}\n",
        std_string_data(_ZNK17storeservicescore18CredentialsRequest5titleEv(
            credReqHandler->obj)),
        std_string_data(_ZNK17storeservicescore18CredentialsRequest7messageEv(
            credReqHandler->obj)),
        need2FA ? "true" : "false");

    prompt_for_password_if_needed();

    const char *password_value = amPassword;
    char *password_with_code = NULL;

    if (need2FA) {
        char code[TWO_FACTOR_CODE_LENGTH + 1] = {0};
        if (args_info.code_from_file_flag) {
            fprintf(stderr, "[!] Enter your 2FA code into rootfs%s/2fa.txt\n", args_info.base_dir_arg);
            fprintf(stderr, "[!] Example command: umask 077; printf '%%s' 114514 > rootfs%s/2fa.txt\n", args_info.base_dir_arg);
            fprintf(stderr, "[!] Waiting for input...\n");
            char path[1024];
            if (snprintf(path, sizeof(path), "%s/2fa.txt", args_info.base_dir_arg) >= (int)sizeof(path)) {
                fprintf(stderr, "[!] 2FA path is too long\n");
                exit(EXIT_FAILURE);
            }

            int timeout_seconds = parse_timeout_seconds("WRAPPER_2FA_TIMEOUT_SECONDS",
                                                        TWO_FACTOR_DEFAULT_TIMEOUT_SECONDS,
                                                        TWO_FACTOR_MAX_TIMEOUT_SECONDS);
            int waited_seconds = 0;
            while (waited_seconds < timeout_seconds) {
                if (file_exists(path)) {
                    int read_result = read_2fa_code_file(path, code);
                    if (read_result >= 0 && remove(path) != 0 && errno != ENOENT) {
                        perror("remove 2fa.txt");
                    }
                    if (read_result < 0) {
                        exit(EXIT_FAILURE);
                    }
                    if (read_result == 1) {
                        fprintf(stderr, "[!] Code file detected! Logging in...\n");
                        break;
                    }
                }

                int remaining = timeout_seconds - waited_seconds;
                int sleep_seconds = remaining < TWO_FACTOR_POLL_SECONDS ? remaining : TWO_FACTOR_POLL_SECONDS;
                sleep((unsigned int)sleep_seconds);
                waited_seconds += sleep_seconds;
            }

            if (code[0] == '\0') {
                fprintf(stderr, "[!] Failed to get a valid 2FA code in %d seconds. Exiting...\n",
                        timeout_seconds);
                exit(EXIT_FAILURE);
            }
        } else {
            char input[64] = {0};
            if (!read_hidden_line("2FA code: ",
                                  "stdin is not a TTY; rerun interactively or use --code-from-file explicitly",
                                  input, sizeof(input))) {
                fprintf(stderr, "[!] Failed to read 2FA code\n");
                exit(EXIT_FAILURE);
            }
            if (!is_valid_2fa_code(input)) {
                fprintf(stderr, "[!] Invalid 2FA code; expected exactly %d digits\n",
                        TWO_FACTOR_CODE_LENGTH);
                exit(EXIT_FAILURE);
            }
            memcpy(code, input, TWO_FACTOR_CODE_LENGTH + 1);
        }

        const size_t passLen = strlen(password_value);
        const size_t codeLen = strlen(code);
        password_with_code = malloc(passLen + codeLen + 1);
        if (password_with_code == NULL) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
        memcpy(password_with_code, password_value, passLen);
        memcpy(password_with_code + passLen, code, codeLen + 1);
        password_value = password_with_code;
    }

    uint8_t *const ptr = malloc(80);
    memset(ptr + 8, 0, 16);
    *(void **)(ptr) =
        &_ZTVNSt6__ndk120__shared_ptr_emplaceIN17storeservicescore19CredentialsResponseENS_9allocatorIS2_EEEE +
        2;
    struct shared_ptr credResp = {.obj = ptr + 24, .ctrl_blk = ptr};
    _ZN17storeservicescore19CredentialsResponseC1Ev(credResp.obj);

    union std_string username = new_std_string(amUsername);
    _ZN17storeservicescore19CredentialsResponse11setUserNameERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(
        credResp.obj, &username);

    union std_string password = new_std_string(password_value);
    _ZN17storeservicescore19CredentialsResponse11setPasswordERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(
        credResp.obj, &password);

    _ZN17storeservicescore19CredentialsResponse15setResponseTypeENS0_12ResponseTypeE(
        credResp.obj, 2);

    _ZN20androidstoreservices28AndroidPresentationInterface25handleCredentialsResponseERKNSt6__ndk110shared_ptrIN17storeservicescore19CredentialsResponseEEE(
        apInf.obj, &credResp);

    free(password_with_code);
}


static inline void init() {
    // srand(time(0));

    // raise(SIGSTOP);
    fprintf(stderr, "[+] starting...\n");
    setenv("ANDROID_DNS_MODE", "local", 1);
    if (args_info.proxy_given) {
        fprintf(stderr, "[+] Using proxy %s\n", args_info.proxy_arg);
        setenv("all_proxy", args_info.proxy_arg, 1);
    }

    static const char *resolvers[2] = {"223.5.5.5", "223.6.6.6"};
    _resolv_set_nameservers_for_net(0, resolvers, 2, ".");

    // static char android_id[16];
    // for (int i = 0; i < 16; ++i) {
    //     android_id[i] = "0123456789abcdef"[rand() % 16];
    // }
    union std_string conf1 = new_std_string(device_infos[8]);
    union std_string conf2 = new_std_string("");
    _ZN14FootHillConfig6configERKNSt6__ndk112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEE(
        &conf1);

    // union std_string root = new_std_string("/");
    // union std_string natLib = new_std_string("/system/lib64/");
    // void *foothill = malloc(120);
    // _ZN8FootHillC2ERKNSt6__ndk112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEES8_(
    //     foothill, &root, &natLib);
    // _ZN8FootHill24defaultContextIdentifierEv(foothill);

    _ZN17storeservicescore10DeviceGUID8instanceEv(&GUID);

    static uint8_t ret[88];
    static unsigned int conf3 = 29;
    static uint8_t conf4 = 1;
    _ZN17storeservicescore10DeviceGUID9configureERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEES9_RKjRKb(
        &ret, GUID.obj, &conf1, &conf2, &conf3, &conf4);
}

static inline struct shared_ptr init_ctx() {
    fprintf(stderr, "[+] initializing ctx...\n");
    char db_path[1024];
    if (!format_base_path(db_path, sizeof(db_path), "/mpl_db")) {
        exit(EXIT_FAILURE);
    }
    union std_string strBuf = new_std_string(db_path);

    struct shared_ptr reqCtx;
    _ZNSt6__ndk110shared_ptrIN17storeservicescore14RequestContextEE11make_sharedIJRNS_12basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEEEEEES3_DpOT_(
        &reqCtx, &strBuf);

    static uint8_t ptr[480];
    *(void **)(ptr) =
        &_ZTVNSt6__ndk120__shared_ptr_emplaceIN17storeservicescore20RequestContextConfigENS_9allocatorIS2_EEEE +
        2;
    struct shared_ptr reqCtxCfg = {.obj = ptr + 32, .ctrl_blk = ptr};

    _ZN17storeservicescore20RequestContextConfigC2Ev(reqCtxCfg.obj);
	// _ZN17storeservicescore20RequestContextConfig9setCPFlagEb(reqCtx.obj, 1);
    _ZN17storeservicescore20RequestContextConfig20setBaseDirectoryPathERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(
        reqCtxCfg.obj, &strBuf);
    strBuf = new_std_string(device_infos[0]);
    _ZN17storeservicescore20RequestContextConfig19setClientIdentifierERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(
        reqCtxCfg.obj, &strBuf);
    strBuf = new_std_string(device_infos[1]);
    _ZN17storeservicescore20RequestContextConfig20setVersionIdentifierERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(
        reqCtxCfg.obj, &strBuf);
    strBuf = new_std_string(device_infos[2]);
    _ZN17storeservicescore20RequestContextConfig21setPlatformIdentifierERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(
        reqCtxCfg.obj, &strBuf);
    strBuf = new_std_string(device_infos[3]);
    _ZN17storeservicescore20RequestContextConfig17setProductVersionERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(
        reqCtxCfg.obj, &strBuf);
    strBuf = new_std_string(device_infos[4]);
    _ZN17storeservicescore20RequestContextConfig14setDeviceModelERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(
        reqCtxCfg.obj, &strBuf);
    strBuf = new_std_string(device_infos[5]);
    _ZN17storeservicescore20RequestContextConfig15setBuildVersionERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(
        reqCtxCfg.obj, &strBuf);
    strBuf = new_std_string(device_infos[6]);
    _ZN17storeservicescore20RequestContextConfig19setLocaleIdentifierERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(
        reqCtxCfg.obj, &strBuf);
    strBuf = new_std_string(device_infos[7]);
    _ZN17storeservicescore20RequestContextConfig21setLanguageIdentifierERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(
        reqCtxCfg.obj, &strBuf);

    _ZN21RequestContextManager9configureERKNSt6__ndk110shared_ptrIN17storeservicescore14RequestContextEEE(
        &reqCtx);
    static uint8_t buf[88];
    _ZN17storeservicescore14RequestContext4initERKNSt6__ndk110shared_ptrINS_20RequestContextConfigEEE(
        &buf, reqCtx.obj, &reqCtxCfg);
    strBuf = new_std_string(args_info.base_dir_arg);
    _ZN17storeservicescore14RequestContext24setFairPlayDirectoryPathERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(
        reqCtx.obj, &strBuf);

    _ZNSt6__ndk110shared_ptrIN20androidstoreservices28AndroidPresentationInterfaceEE11make_sharedIJEEES3_DpOT_(
        &apInf);

    _ZN20androidstoreservices28AndroidPresentationInterface16setDialogHandlerEPFvlNSt6__ndk110shared_ptrIN17storeservicescore14ProtocolDialogEEENS2_INS_36AndroidProtocolDialogResponseHandlerEEEE(
        apInf.obj, &dialogHandler);

    _ZN20androidstoreservices28AndroidPresentationInterface21setCredentialsHandlerEPFvNSt6__ndk110shared_ptrIN17storeservicescore18CredentialsRequestEEENS2_INS_33AndroidCredentialsResponseHandlerEEEE(
        apInf.obj, &credentialHandler);

    _ZN17storeservicescore14RequestContext24setPresentationInterfaceERKNSt6__ndk110shared_ptrINS_21PresentationInterfaceEEE(
        reqCtx.obj, &apInf);

    return reqCtx;
}

extern void *endLeaseCallback;
extern void *pbErrCallback;

inline static uint8_t login(struct shared_ptr reqCtx) {
    fprintf(stderr, "[+] logging in...\n");
    char storefront_path[1024];
    char music_token_path[1024];
    if (!format_base_path(storefront_path, sizeof(storefront_path), "/STOREFRONT_ID") ||
        !format_base_path(music_token_path, sizeof(music_token_path), "/MUSIC_TOKEN")) {
        return 0;
    }
    if (file_exists(storefront_path)) {
        remove(storefront_path);
    }
    if (file_exists(music_token_path)) {
        remove(music_token_path);
    }
    struct shared_ptr flow;
    _ZNSt6__ndk110shared_ptrIN17storeservicescore16AuthenticateFlowEE11make_sharedIJRNS0_INS1_14RequestContextEEEEEES3_DpOT_(
        &flow, &reqCtx);
    _ZN17storeservicescore16AuthenticateFlow3runEv(flow.obj);
    struct shared_ptr *resp =
        _ZNK17storeservicescore16AuthenticateFlow8responseEv(flow.obj);
    if (resp == NULL || resp->obj == NULL)
        return 0;
    const int respType =
        _ZNK17storeservicescore20AuthenticateResponse12responseTypeEv(
            resp->obj);
    if (respType != 6) {
        const char *customer_msg = std_string_data(
            _ZNK17storeservicescore20AuthenticateResponse15customerMessageEv(resp->obj));
        if (customer_msg && *customer_msg) {
            fprintf(stderr, "[!] auth failed: server returned a customer message (%zu bytes)\n",
                    strlen(customer_msg));
        }

        struct shared_ptr *err = _ZNK17storeservicescore20AuthenticateResponse5errorEv(resp->obj);
        if (err != NULL && err->obj != NULL) {
            int code = _ZNK17storeservicescore19StoreErrorCondition9errorCodeEv(err->obj);
            fprintf(stderr, "[!] auth error: code=%d, message omitted\n", code);
        } else {
            fprintf(stderr, "[!] auth failed: response type %d\n", respType);
        }
    }
    return respType == 6;
    // struct shared_ptr subStatMgr;
    // _ZN20androidstoreservices30SVSubscriptionStatusMgrFactory6createEv(&subStatMgr);
    // struct shared_ptr data;
    // int method = 2;
    // _ZN20androidstoreservices27SVSubscriptionStatusMgrImpl33checkSubscriptionStatusFromSourceERKNSt6__ndk110shared_ptrIN17storeservicescore14RequestContextEEERKNS_23SVSubscriptionStatusMgr26SVSubscriptionStatusSourceE(&data,
    // subStatMgr.obj, &reqCtx, &method);
    // return 1;
}

static inline uint8_t readfull(const int connfd, void *const buf,
                               const size_t size) {
    size_t red = 0;
    while (size > red) {
        const ssize_t b = read(connfd, ((uint8_t *)buf) + red, size - red);
        if (b <= 0)
            return 0;
        red += b;
    }
    return 1;
}

static inline void writefull(const int connfd, void *const buf,
                             const size_t size) {
    size_t red = 0;
    while (size > red) {
        const ssize_t b = write(connfd, ((uint8_t *)buf) + red, size - red);
        if (b <= 0) {
            perror("write");
            break;
        }
        red += b;
    }
}

static void *FHinstance = NULL;
static void *preshareCtx = NULL;

inline static void *getKdContext(const char *const adam,
                                 const char *const uri) {
    uint8_t isPreshare = (strcmp("0", adam) == 0);
    if (isPreshare && preshareCtx != NULL) {
        return preshareCtx;
    }
    fprintf(stderr, "[.] adamId: %s, key uri bytes: %zu\n", adam, strlen(uri));

    union std_string defaultId = new_std_string(adam);
    union std_string keyUri = new_std_string(uri);
    union std_string keyFormat =
        new_std_string("com.apple.streamingkeydelivery");
    union std_string keyFormatVer = new_std_string("1");
    union std_string serverUri = new_std_string(
        "https://play.itunes.apple.com/WebObjects/MZPlay.woa/music/fps");
    union std_string protocolType = new_std_string("simplified");
    union std_string fpsCert = new_std_string(fairplayCert);

    struct shared_ptr persistK = {.obj = NULL};
    _ZN21SVFootHillSessionCtrl16getPersistentKeyERKNSt6__ndk112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEES8_S8_S8_S8_S8_S8_S8_(
        &persistK, FHinstance, &defaultId, &defaultId, &keyUri, &keyFormat,
        &keyFormatVer, &serverUri, &protocolType, &fpsCert);

    if (persistK.obj == NULL)
        return NULL;

    struct shared_ptr SVFootHillPContext;
    _ZN21SVFootHillSessionCtrl14decryptContextERKNSt6__ndk112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEERKN11SVDecryptor15SVDecryptorTypeERKb(
        &SVFootHillPContext, FHinstance, persistK.obj);

    if (SVFootHillPContext.obj == NULL)
        return NULL;

    void *kdContext =
        *_ZNK18SVFootHillPContext9kdContextEv(SVFootHillPContext.obj);
    if (kdContext != NULL && isPreshare)
        preshareCtx = kdContext;
    return kdContext;
}

void refresh_decrypt_ctx() {
    uint8_t autom = 1;
    _ZN22SVPlaybackLeaseManager12requestLeaseERKb(leaseMgr, &autom);
    _ZN21SVFootHillSessionCtrl16resetAllContextsEv(FHinstance);
    preshareCtx = NULL;
    preshareCtx = getKdContext("0", "skd://itunes.apple.com/P000000000/s1/e1");
    fprintf(stderr, "[!] refreshed context\n");
}

void handle(const int connfd) {
    while (1) {
        uint8_t adamSize;
        if (!readfull(connfd, &adamSize, sizeof(uint8_t)))
            return;
        if (adamSize <= 0)
            return;
        if (adamSize > MAX_DECRYPT_ADAM_ID_BYTES) {
            fprintf(stderr, "[.] decrypt request rejected: adamId is %u bytes; max is %u\n",
                    (unsigned int)adamSize, (unsigned int)MAX_DECRYPT_ADAM_ID_BYTES);
            return;
        }

        char adam[adamSize + 1];
        if (!readfull(connfd, adam, adamSize))
            return;
        adam[adamSize] = '\0';

        uint8_t uri_size;
        if (!readfull(connfd, &uri_size, sizeof(uint8_t)))
            return;
        if (uri_size == 0 || uri_size > MAX_DECRYPT_KEY_URI_BYTES) {
            fprintf(stderr, "[.] decrypt request rejected: key uri is %u bytes; max is %u\n",
                    (unsigned int)uri_size, (unsigned int)MAX_DECRYPT_KEY_URI_BYTES);
            return;
        }

        char uri[uri_size + 1];
        if (!readfull(connfd, uri, uri_size))
            return;
        uri[uri_size] = '\0';

        void **const kdContext = getKdContext(adam, uri);
        if (kdContext == NULL)
            return;

        while (1) {
            uint32_t size;
            if (!readfull(connfd, &size, sizeof(uint32_t))) {
                perror("read");
                return;
            }

            if (size <= 0)
                break;
            if (size > MAX_DECRYPT_SAMPLE_BYTES) {
                fprintf(stderr, "[.] decrypt sample rejected: %u bytes; max is %u\n",
                        (unsigned int)size, (unsigned int)MAX_DECRYPT_SAMPLE_BYTES);
                return;
            }

            void *sample = malloc(size);
            if (sample == NULL) {
                perror("malloc");
                return;
            }
            if (!readfull(connfd, sample, size)) {
                free(sample);
                perror("read");
                return;
            }

            NfcRKVnxuKZy04KWbdFu71Ou(*kdContext, 5, sample, sample, size);
            writefull(connfd, sample, size);
            free(sample);
        }
    }
}

extern uint8_t handle_cpp(int);

static int is_transient_accept_error(const int err) {
    return err == ENETDOWN || err == EPROTO || err == ENOPROTOOPT ||
           err == EHOSTDOWN || err == ENONET || err == EHOSTUNREACH ||
           err == EOPNOTSUPP || err == ENETUNREACH;
}

static int open_listening_socket(const char *label, const int port) {
    const int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
    if (fd == -1) {
        perror("socket");
        return -1;
    }

    const int optval = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval)) == -1) {
        perror("setsockopt");
        close(fd);
        return -1;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, args_info.host_arg, &serv_addr.sin_addr) != 1) {
        fprintf(stderr, "[!] invalid host address: %s\n", args_info.host_arg);
        close(fd);
        return -1;
    }

    serv_addr.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1) {
        perror("bind");
        close(fd);
        return -1;
    }

    if (listen(fd, 5) == -1) {
        perror("listen");
        close(fd);
        return -1;
    }

    fprintf(stderr, "[!] listening %s on %s:%d\n", label, args_info.host_arg, port);
    return fd;
}

static int accept_client(const int fd) {
    struct sockaddr_in peer_addr;
    socklen_t peer_addr_size = sizeof(peer_addr);

    while (1) {
        const int connfd = accept4(fd, (struct sockaddr *)&peer_addr,
                                   &peer_addr_size, SOCK_CLOEXEC);
        if (connfd != -1) {
            return connfd;
        }
        if (is_transient_accept_error(errno)) {
            continue;
        }
        perror("accept4");
        return -1;
    }
}

inline static int new_socket() {
    const int fd = open_listening_socket("decrypt", args_info.decrypt_port_arg);
    if (fd == -1) {
        return EXIT_FAILURE;
    }
    // close(STDOUT_FILENO);

    while (1) {
        const int connfd = accept_client(fd);
        if (connfd == -1) {
            return EXIT_FAILURE;
        }

        if (!handle_cpp(connfd)) {
            uint8_t autom = 1;
            _ZN22SVPlaybackLeaseManager12requestLeaseERKb(leaseMgr, &autom);
        }
        // if (sigsetjmp(catcher.env, 0) == 0) {
        //     catcher.do_jump = 1;
        //     handle(connfd);
        // }
        // catcher.do_jump = 0;

        if (close(connfd) == -1) {
            perror("close");
            return EXIT_FAILURE;
        }
    }
}


const char* get_m3u8_method_download(struct shared_ptr reqCtx, unsigned long adam) {
    void *purchase_request = malloc(1024);
    _ZN17storeservicescore15PurchaseRequestC2ERKNSt6__ndk110shared_ptrINS_14RequestContextEEE(purchase_request, &reqCtx);
    _ZN17storeservicescore15PurchaseRequest23setProcessDialogActionsEb(purchase_request, 1);
    union std_string urlBagKey = new_std_string("subDownload");
    _ZN17storeservicescore15PurchaseRequest12setURLBagKeyERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(purchase_request, &urlBagKey);
    char *buyParametersStr = malloc(128);
    sprintf(buyParametersStr, "salableAdamId=%lu&price=0&pricingParameters=SUBS&productType=S", adam);
    union std_string buyParameters = new_std_string(buyParametersStr);
    _ZN17storeservicescore15PurchaseRequest16setBuyParametersERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(purchase_request, &buyParameters);
    _ZN17storeservicescore15PurchaseRequest3runEv(purchase_request);
    struct shared_ptr *response = _ZNK17storeservicescore15PurchaseRequest8responseEv(purchase_request);
    struct shared_ptr *error = _ZN17storeservicescore16PurchaseResponse5errorEv(response->obj);;
    if (error->obj == NULL) {
        struct std_vector items = _ZNK17storeservicescore16PurchaseResponse5itemsEv(response->obj);
        struct shared_ptr *firstItem = items.begin;
        struct std_vector assets = _ZNK17storeservicescore12PurchaseItem6assetsEv(firstItem->obj);
        struct shared_ptr *lastAsset = (struct shared_ptr *)assets.end - 1;
        union std_string *url_str = malloc(sizeof(union std_string));
        _ZNK17storeservicescore13PurchaseAsset3URLEv(url_str, lastAsset->obj);
        const char *url = std_string_data(url_str);
        if (url) {
            char *result = strdup(url);  // Make a copy
            free(url_str);
            return result;
        }
    } 
    return NULL;
}


const char* get_m3u8_method_play(uint8_t leaseMgr[16], unsigned long adam) {
    union std_string HLS = new_std_string_short_mode("HLS");
    struct std_vector HLSParam = new_std_vector(&HLS);
    static uint8_t z0 = 0;
    struct shared_ptr ptr_result;
    _ZN22SVPlaybackLeaseManager12requestAssetERKmRKNSt6__ndk16vectorINS2_12basic_stringIcNS2_11char_traitsIcEENS2_9allocatorIcEEEENS7_IS9_EEEERKb(
        &ptr_result, leaseMgr, &adam, &HLSParam, &z0
    );
    
    if (ptr_result.obj == NULL) {
        return NULL;
    }

    if (_ZNK23SVPlaybackAssetResponse13hasValidAssetEv(ptr_result.obj)) {
        struct shared_ptr *playbackAsset = _ZNK23SVPlaybackAssetResponse13playbackAssetEv(ptr_result.obj);
        if (playbackAsset == NULL || playbackAsset->obj == NULL) {
            return NULL;
        }

        union std_string *m3u8 = malloc(sizeof(union std_string));
        if (m3u8 == NULL) {
            return NULL;
        }

        void *playbackObj = playbackAsset->obj;
        _ZNK17storeservicescore13PlaybackAsset9URLStringEv(m3u8, playbackObj);

        if (m3u8 == NULL || std_string_data(m3u8) == NULL) {
            free(m3u8);
            return NULL;
        }
        
        const char *m3u8_str = std_string_data(m3u8);
        if (m3u8_str) {
            char *result = strdup(m3u8_str);  // Make a copy
            free(m3u8);
            return result;
        } else {
            return NULL;
        }
    } else {
        return NULL;
    }
}

void handle_m3u8(const int connfd) {
    while (1)
    {
        uint8_t adamSize;
        if (!readfull(connfd, &adamSize, sizeof(uint8_t))) {
            return;
        }
        if (adamSize <= 0) {
            return;
        }
        if (adamSize > MAX_M3U8_ADAM_ID_BYTES) {
            fprintf(stderr, "[.] m3u8 request rejected: adamId is %u bytes; max is %u\n",
                    (unsigned int)adamSize, (unsigned int)MAX_M3U8_ADAM_ID_BYTES);
            return;
        }
        char adam[adamSize + 1];
        if (!readfull(connfd, adam, adamSize)) {
            return;
        }
        adam[adamSize] = '\0';
        char *ptr = NULL;
        errno = 0;
        unsigned long adamID = strtoul(adam, &ptr, 10);
        if (errno != 0 || ptr == adam || *ptr != '\0') {
            fprintf(stderr, "[.] invalid adamId: %s\n", adam);
            writefull(connfd, "\n", 1);
            continue;
        }
        const char *m3u8;
        if (offlineFlag) {
            m3u8 = get_m3u8_method_download(reqCtx, adamID);
        } else {
            m3u8 = get_m3u8_method_play(leaseMgr, adamID);
        }
        if (m3u8 == NULL) {
            fprintf(stderr, "[.] failed to get m3u8 of adamId: %ld\n", adamID);
            writefull(connfd, "\n", 1);
        } else {
            fprintf(stderr, "[.] m3u8 adamId: %ld, url bytes: %zu\n",
                    adamID, strlen(m3u8));
            writefull(connfd, (void *)m3u8, strlen(m3u8));
            writefull(connfd, "\n", 1);
            free((void *)m3u8);
        }
    }
}

static inline void *new_socket_m3u8(void *args) {
    (void)args;
    const int fd = open_listening_socket("m3u8 request", args_info.m3u8_port_arg);
    if (fd == -1) {
        return NULL;
    }
    // close(STDOUT_FILENO);

    while (1) {
        const int connfd = accept_client(fd);
        if (connfd == -1) {
            continue;
        }

        handle_m3u8(connfd);

        if (close(connfd) == -1) {
            perror("close");
        }
    }
}

void handle_account(const int connfd)
{
    char buffer[MAX_ACCOUNT_REQUEST_BYTES + 1];
    ssize_t n = read(connfd, buffer, sizeof(buffer));
    if (n <= 0) {
        return;
    }
    if ((size_t)n > MAX_ACCOUNT_REQUEST_BYTES) {
        const char *error_response = "HTTP/1.1 413 Payload Too Large\r\nContent-Type: application/json\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        writefull(connfd, (void *)error_response, strlen(error_response));
        return;
    }
    buffer[n] = '\0';

    // Parse HTTP request (simple check for GET)
    if (strncmp(buffer, "GET", 3) != 0 && strncmp(buffer, "POST", 4) != 0) {
        const char *error_response = "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\nContent-Length: 0\r\n\r\n";
        writefull(connfd, (void *)error_response, strlen(error_response));
        return;
    }

    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        fprintf(stderr, "[.] failed to allocate account response JSON\n");
        const char *error_response = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\nContent-Length: 0\r\n\r\n";
        writefull(connfd, (void *)error_response, strlen(error_response));
        return;
    }

    if (cJSON_AddStringToObject(json, "storefront_id", g_storefront_id ? g_storefront_id : "") == NULL ||
        cJSON_AddStringToObject(json, "dev_token", g_dev_token ? g_dev_token : "") == NULL ||
        cJSON_AddStringToObject(json, "music_token", g_music_token ? g_music_token : "") == NULL) {
        fprintf(stderr, "[.] failed to populate account response JSON\n");
        cJSON_Delete(json);
        const char *error_response = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\nContent-Length: 0\r\n\r\n";
        writefull(connfd, (void *)error_response, strlen(error_response));
        return;
    }

    char *json_body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (json_body == NULL) {
        fprintf(stderr, "[.] failed to format account response JSON\n");
        const char *error_response = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\nContent-Length: 0\r\n\r\n";
        writefull(connfd, (void *)error_response, strlen(error_response));
        return;
    }

    size_t json_len = strlen(json_body);
    char http_response[256];
    int header_len = snprintf(http_response, sizeof(http_response),
                              "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
                              json_len);
    if (header_len < 0 || (size_t)header_len >= sizeof(http_response)) {
        fprintf(stderr, "[.] failed to format account response headers\n");
        cJSON_free(json_body);
        const char *error_response = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\nContent-Length: 0\r\n\r\n";
        writefull(connfd, (void *)error_response, strlen(error_response));
        return;
    }

    fprintf(stderr, "[.] returning account info, storefront: %s\n", g_storefront_id);
    writefull(connfd, http_response, (size_t)header_len);
    writefull(connfd, json_body, json_len);

    cJSON_free(json_body);
}

static inline void *new_socket_account(void *args)
{
    (void)args;
    const int fd = open_listening_socket("account info request", args_info.account_port_arg);
    if (fd == -1)
    {
        return NULL;
    }

    while (1)
    {
        const int connfd = accept_client(fd);
        if (connfd == -1)
        {
            continue;
        }

        handle_account(connfd);

        if (close(connfd) == -1)
        {
            perror("close");
        }
    }
}

char* get_account_storefront_id(struct shared_ptr reqCtx) {
    union std_string *region = malloc(sizeof(union std_string));
    if (region == NULL) {
        perror("malloc storefront");
        return NULL;
    }
    struct shared_ptr urlbag = {.obj = 0x0, .ctrl_blk = 0x0};
    _ZNK17storeservicescore14RequestContext20storeFrontIdentifierERKNSt6__ndk110shared_ptrINS_6URLBagEEE(region, reqCtx.obj, &urlbag);
    const char *region_str = std_string_data(region);
    if (region_str) {
        char *result = strdup(region_str); 
        if (result == NULL) {
            perror("strdup storefront");
        }
        free(region);
        return result;
    } 
    free(region);
    return NULL;
}

void write_storefront_id(void) {
    char path[1024];
    if (!format_base_path(path, sizeof(path), "/STOREFRONT_ID")) {
        return;
    }
    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        perror("fopen STOREFRONT_ID");
        return;
    }
    fprintf(stderr, "[+] StoreFront ID: %s\n", g_storefront_id);
    fprintf(fp, "%s", g_storefront_id);
    fclose(fp);
}

char *get_guid() {
    char *ret[2];
    _ZN17storeservicescore10DeviceGUID4guidEv(ret, GUID.obj);
    char *guid = _ZNK13mediaplatform4Data5bytesEv(ret[0]);
    return guid;
}

long long getCurrentTimeMillis() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000LL + tv.tv_usec / 1000;
}


char *get_music_user_token(char *guid, char *authToken, struct shared_ptr reqCtx){
    uint8_t ptr[480];
    *(void **)(ptr) =
        &_ZTVNSt6__ndk120__shared_ptr_emplaceIN13mediaplatform11HTTPMessageENS_9allocatorIS2_EEEE +
        2;
    struct shared_ptr httpMessage = {.obj = ptr + 32, .ctrl_blk = ptr};
    union std_string url = new_std_string("https://play.itunes.apple.com/WebObjects/MZPlay.woa/wa/createMusicToken");
    union std_string method = new_std_string("POST");
    _ZN13mediaplatform11HTTPMessageC2ENSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEES7_(httpMessage.obj, &url, &method);
    union std_string contentTypeHeader = new_std_string("Content-Type");
    union std_string contentTypeValue = new_std_string("application/json; charset=UTF-8");
    _ZN13mediaplatform11HTTPMessage9setHeaderERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEES9_(httpMessage.obj, &contentTypeHeader, &contentTypeValue);
    union std_string expectHeader = new_std_string("Expect");
    union std_string expectValue = new_std_string("");
    _ZN13mediaplatform11HTTPMessage9setHeaderERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEES9_(httpMessage.obj, &expectHeader, &expectValue);
    union std_string bundleIdHeader = new_std_string("X-Apple-Requesting-Bundle-Id");
    union std_string bundleIdValue = new_std_string("com.apple.android.music");
    _ZN13mediaplatform11HTTPMessage9setHeaderERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEES9_(httpMessage.obj, &bundleIdHeader, &bundleIdValue);
    union std_string bundleVersionHeader = new_std_string("X-Apple-Requesting-Bundle-Version");
    union std_string bundleVersionValue = new_std_string("Music/4.9 Android/10 model/Samsung S9 build/7663313 (dt:66)");
    _ZN13mediaplatform11HTTPMessage9setHeaderERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEES9_(httpMessage.obj, &bundleVersionHeader, &bundleVersionValue);
    long long acceptance_time = getCurrentTimeMillis();
    int body_len = snprintf(NULL, 0,
                            "{\"guid\":\"%s\",\"assertion\":\"%s\",\"tcc-acceptance-date\":\"%lld\"}",
                            guid, authToken, acceptance_time);
    if (body_len < 0) {
        fprintf(stderr, "[!] failed to format createMusicToken request body\n");
        return NULL;
    }

    size_t body_size = (size_t)body_len + 1;
    char *body = (char *)malloc(body_size);
    if (body == NULL) {
        perror("malloc createMusicToken body");
        return NULL;
    }

    snprintf(body, body_size,
             "{\"guid\":\"%s\",\"assertion\":\"%s\",\"tcc-acceptance-date\":\"%lld\"}",
             guid, authToken, acceptance_time);

    _ZN13mediaplatform11HTTPMessage11setBodyDataEPcm(httpMessage.obj, body, strlen(body));
    free(body);
    uint8_t urlRequest[512];
    _ZN17storeservicescore10URLRequestC2ERKNSt6__ndk110shared_ptrIN13mediaplatform11HTTPMessageEEERKNS2_INS_14RequestContextEEE(urlRequest, &httpMessage, &reqCtx);
    _ZN17storeservicescore10URLRequest3runEv(urlRequest);
    struct shared_ptr *err = _ZNK17storeservicescore10URLRequest5errorEv(urlRequest);
    if (err->obj != NULL) {
        int code = _ZNK17storeservicescore19StoreErrorCondition9errorCodeEv(err->obj);
        fprintf(stderr, "[!] createMusicToken error: code=%d, message omitted\n", code);
        return NULL;
    }
    struct shared_ptr *urlResp = _ZNK17storeservicescore10URLRequest8responseEv(urlRequest);
    struct shared_ptr *resp = _ZNK17storeservicescore11URLResponse18underlyingResponseEv(urlResp->obj);
    void *http_message_obj = resp->obj;
    void** data_ptr_location = (void**)((char*)http_message_obj + 48);
    void* data_ptr = *data_ptr_location;
    char *respBody = _ZNK13mediaplatform4Data5bytesEv(data_ptr);
    cJSON *json = cJSON_Parse(respBody);
    if (json == NULL) {
        fprintf(stderr, "[!] createMusicToken error: invalid JSON response\n");
        if (respBody != NULL) {
            fprintf(stderr, "[!] createMusicToken response bytes: %zu\n", strlen(respBody));
        } else {
            fprintf(stderr, "[!] createMusicToken response body is empty\n");
        }
        return NULL;
    }
    cJSON *token_obj = cJSON_GetObjectItemCaseSensitive(json, "music_token");
    char *token = cJSON_GetStringValue(token_obj);
    if (token == NULL) {
        const char *err_desc = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, "error_description"));
        const char *err_code = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, "error"));
        fprintf(stderr, "[!] createMusicToken failed: error=%s, description bytes=%zu\n",
                err_code ? err_code : "?",
                err_desc ? strlen(err_desc) : 0);
        if (err_desc != NULL) {
            fprintf(stderr, "[!] createMusicToken error description omitted\n");
        }
        cJSON_Delete(json);
        return NULL;
    }
    char *result = strdup(token);
    if (result == NULL) {
        perror("strdup music_token");
    }
    cJSON_Delete(json);
    return result;
}


char* get_dev_token(struct shared_ptr reqCtx) {
    uint8_t ptr[480];
    *(void **)(ptr) =
        &_ZTVNSt6__ndk120__shared_ptr_emplaceIN13mediaplatform11HTTPMessageENS_9allocatorIS2_EEEE +
        2;
    struct shared_ptr httpMessage = {.obj = ptr + 32, .ctrl_blk = ptr};
    union std_string url = new_std_string("https://sf-api-token-service.itunes.apple.com/apiToken");
    union std_string method = new_std_string("GET");
    _ZN13mediaplatform11HTTPMessageC2ENSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEES7_(httpMessage.obj, &url, &method);
    uint8_t urlRequest[512];
    _ZN17storeservicescore10URLRequestC2ERKNSt6__ndk110shared_ptrIN13mediaplatform11HTTPMessageEEERKNS2_INS_14RequestContextEEE(urlRequest, &httpMessage, &reqCtx);
    union std_string clientIdName = new_std_string("clientId");
    union std_string clientIdValue = new_std_string("musicAndroid");
    _ZN17storeservicescore10URLRequest19setRequestParameterERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEES9_(urlRequest, &clientIdName, &clientIdValue);
    union std_string versionName = new_std_string("version");
    union std_string versionValue = new_std_string("1");
    _ZN17storeservicescore10URLRequest19setRequestParameterERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEES9_(urlRequest, &versionName, &versionValue);
    _ZN17storeservicescore10URLRequest3runEv(urlRequest);
    struct shared_ptr *err = _ZNK17storeservicescore10URLRequest5errorEv(urlRequest);
    if (err->obj != NULL) {
        int code = _ZNK17storeservicescore19StoreErrorCondition9errorCodeEv(err->obj);
        fprintf(stderr, "[!] devToken error: code=%d, message omitted\n", code);
        return NULL;
    }
    struct shared_ptr *urlResp = _ZNK17storeservicescore10URLRequest8responseEv(urlRequest);
    struct shared_ptr *resp = _ZNK17storeservicescore11URLResponse18underlyingResponseEv(urlResp->obj);
    void *http_message_obj = resp->obj;
    void** data_ptr_location = (void**)((char*)http_message_obj + 48);
    void* data_ptr = *data_ptr_location;
    char *respBody = _ZNK13mediaplatform4Data5bytesEv(data_ptr);
    cJSON *json = cJSON_Parse(respBody);
    if (json == NULL) {
        fprintf(stderr, "[!] devToken error: invalid JSON response\n");
        return NULL;
    }
    cJSON *token_obj = cJSON_GetObjectItemCaseSensitive(json, "token");
    char *token = cJSON_GetStringValue(token_obj);
    if (token == NULL) {
        fprintf(stderr, "[!] devToken error: token field missing in response\n");
        cJSON_Delete(json);
        return NULL;
    }
    char *result = strdup(token);
    if (result == NULL) {
        perror("strdup dev token");
    }
    cJSON_Delete(json);
    return result;
}

void write_music_token(void) {
    char path[1024];
    if (!format_base_path(path, sizeof(path), "/MUSIC_TOKEN")) {
        return;
    }

    int token_file_available = 0;
    if (file_exists(path)) {
        FILE *fp = fopen(path, "r");
        if (NULL != fp) {
            fseek (fp, 0, SEEK_END);
            long size = ftell(fp);

            if (0 != size) {
                token_file_available = 1;
            }
            fclose(fp);
        }
    }
    if (token_file_available) {
        char token[256];
        FILE *fp = fopen(path, "r");
        if (fp == NULL) {
            perror("fopen MUSIC_TOKEN");
            return;
        }
        if (fgets(token, sizeof(token), fp) == NULL) {
            fclose(fp);
            fprintf(stderr, "[!] failed to read MUSIC_TOKEN\n");
            return;
        }
        fclose(fp);
        fprintf(stderr, "[+] Music token cache exists\n");
        return;
    }
    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        perror("fopen MUSIC_TOKEN");
        return;
    }
    fprintf(stderr, "[+] Music token cached\n");
    fprintf(fp, "%s", g_music_token);
    fclose(fp);
}

int offline_available() {
    struct shared_ptr fairplay = {.obj = NULL, .ctrl_blk = NULL};
    _ZN17storeservicescore14RequestContext8fairPlayEv(&fairplay, reqCtx.obj);
    if (fairplay.obj == NULL) {
        fprintf(stderr, "[.] offline channel unavailable: missing FairPlay context\n");
        return 0;
    }

    struct std_vector fairplay_status = _ZN17storeservicescore8FairPlay21getSubscriptionStatusEv(fairplay.obj);
    char *begin_ptr = (char *)fairplay_status.begin;
    char *end_ptr = (char *)fairplay_status.end;
    if (begin_ptr == NULL || end_ptr == NULL || end_ptr <= begin_ptr) {
        fprintf(stderr, "[.] offline channel unavailable: empty FairPlay status\n");
        return 0;
    }

    if ((end_ptr - begin_ptr) < 32) {
        fprintf(stderr, "[.] offline channel unavailable: incomplete FairPlay status\n");
        return 0;
    }

    char *second_item_ptr = begin_ptr + 16;
    int state = *(int*)((char*)second_item_ptr + 8);
    if (state == 2 || state == 3) { // kFPSubscriptionCanPlayContent, kFPSubscriptionCanStreamAndPlayContent
        return 1;
    } 
    return 0;
}

int main(int argc, char *argv[]) {
    if (cmdline_parser(argc, argv, &args_info) != 0) {
        return EXIT_FAILURE;
    }
    char *copy_that_needs_to_be_freed = NULL;
    int device_info_count = split_string_safe(args_info.device_info_arg, "/", device_infos, 9, &copy_that_needs_to_be_freed);
    if (device_info_count != 9) {
        fprintf(stderr, "[!] device-info must contain 9 slash-separated fields\n");
        free(copy_that_needs_to_be_freed);
        return EXIT_FAILURE;
    }

    #ifndef MyRelease
    install_hooks();
    #endif

    init();
    reqCtx = init_ctx();
    fprintf(stderr, "[+] ctx initialized\n");
    if (args_info.login_given) {
        char *separator = strchr(args_info.login_arg, ':');
        if (args_info.login_arg[0] == '\0' || separator == args_info.login_arg) {
            fprintf(stderr, "[!] login username must not be empty\n");
            return EXIT_FAILURE;
        }
        amUsername = args_info.login_arg;
        if (separator != NULL) {
            *separator = '\0';
            if (separator[1] != '\0') {
                amPassword = separator + 1;
            }
        }
    }
    if (args_info.login_given) {
        if (!login(reqCtx)) {
            clear_prompted_password();
            fprintf(stderr, "[!] login failed\n");
            return EXIT_FAILURE;
        }
        clear_prompted_password();
    }
    fprintf(stderr, "[+] requesting playback lease...\n");
    _ZN22SVPlaybackLeaseManagerC2ERKNSt6__ndk18functionIFvRKiEEERKNS1_IFvRKNS0_10shared_ptrIN17storeservicescore19StoreErrorConditionEEEEEE(
        leaseMgr, &endLeaseCallback, &pbErrCallback);
    uint8_t autom = 1;
    _ZN22SVPlaybackLeaseManager25refreshLeaseAutomaticallyERKb(leaseMgr, &autom);
    _ZN22SVPlaybackLeaseManager12requestLeaseERKb(leaseMgr, &autom);
    FHinstance = _ZN21SVFootHillSessionCtrl8instanceEv();
    fprintf(stderr, "[+] playback lease requested\n");

    const char *disable_offline = getenv("WRAPPER_DISABLE_OFFLINE");
    if (disable_offline != NULL && strcmp(disable_offline, "1") == 0) {
        offlineFlag = 0;
        fprintf(stderr, "[.] offline channel disabled by WRAPPER_DISABLE_OFFLINE\n");
    } else {
        offlineFlag = offline_available();
    }
    if (offlineFlag) {
        fprintf(stderr, "[+] This account supports offline channel\n");
    } else {
        fprintf(stderr, "[.] using streaming channel\n");
    }

    // Cache account info
    g_storefront_id = get_account_storefront_id(reqCtx);
    if (g_storefront_id == NULL) {
        fprintf(stderr, "[!] failed to get storefront ID\n");
        return EXIT_FAILURE;
    }
    g_dev_token = get_dev_token(reqCtx);
    if (g_dev_token == NULL) {
        fprintf(stderr, "[!] failed to get dev token\n");
        return EXIT_FAILURE;
    }
    g_music_token = get_music_user_token(get_guid(), g_dev_token, reqCtx);
    if (g_music_token == NULL) {
        fprintf(stderr, "[!] failed to get music token\n");
        return EXIT_FAILURE;
    }
    fprintf(stderr, "[+] account info cached successfully\n");

    write_storefront_id();
    write_music_token();

    pthread_t m3u8_thread;
    int thread_err = pthread_create(&m3u8_thread, NULL, &new_socket_m3u8, NULL);
    if (thread_err != 0) {
        errno = thread_err;
        perror("pthread_create m3u8");
        return EXIT_FAILURE;
    }
    pthread_detach(m3u8_thread);

    pthread_t account_thread;
    thread_err = pthread_create(&account_thread, NULL, &new_socket_account, NULL);
    if (thread_err != 0) {
        errno = thread_err;
        perror("pthread_create account");
        return EXIT_FAILURE;
    }
    pthread_detach(account_thread);

    return new_socket();
}
