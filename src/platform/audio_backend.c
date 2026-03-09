/* SDL2 audio callback — stub */
#include <stddef.h>

void audio_backend_init(void) {}
void audio_backend_shutdown(void) {}
void audio_backend_submit(const void *samples, size_t count) { (void)samples; (void)count; }
