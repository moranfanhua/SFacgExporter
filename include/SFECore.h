#pragma once
#ifdef _WIN32
#define SF_API __declspec(dllexport)
#else
#define SF_API
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef void(__cdecl *sf_event_callback)(const char *json_utf8, void *user);
SF_API void *sf_create(const char *config_utf8, sf_event_callback callback, void *user);
SF_API const char *sf_init_error(void);
SF_API char *sf_call(void *core, const char *request_utf8);
SF_API void sf_free(char *value);
SF_API void sf_reset_cancel(void *core);
SF_API void sf_cancel(void *core);
SF_API void sf_destroy(void *core);
#ifdef __cplusplus
}
#endif
