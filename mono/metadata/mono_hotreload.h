#ifndef MONO_METADATA_MONO_HOTRELOAD_H
#define MONO_METADATA_MONO_HOTRELOAD_H

#include <stdint.h>
#include <mono/utils/mono-compiler.h>
#include <mono/utils/mono-forward.h>
#include <mono/utils/mono-publib.h>

MONO_BEGIN_EXTERN_C;

MONO_API void *mono_hr_create_domain(const char *name);

MONO_API int mono_hr_unload_domain(void *handle);

/* ICall: load an assembly (raw bytes) into the specified domain.
   Returns a Reflection.Assembly for that domain, or NULL on failure. */
MONO_API struct _MonoReflectionAssembly *ves_icall_mono_hr_load_plugin(void *handle, MonoArray *data_arr);

MONO_API struct _MonoAssembly *mono_hr_try_get_loaded_assembly(const char *simple_name);

MONO_END_EXTERN_C;

#endif /* MONO_METADATA_MONO_HOTRELOAD_H */