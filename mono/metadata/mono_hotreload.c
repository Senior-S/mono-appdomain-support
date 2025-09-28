#include "config.h"
#include <glib.h>
#include <stdint.h>

#include "mono/metadata/appdomain.h"
#include "mono/metadata/assembly.h"
#include "mono/metadata/image.h"
#include "mono/metadata/object-internals.h"
#include "mono/metadata/reflection-internals.h"
#include "mono/metadata/mono_hotreload.h"

typedef struct {
	MonoDomain *domain;
	GHashTable *assemblies;
} HrDomain;

static GHashTable *s_domains;
static GHashTable *s_domains_by_ptr = NULL;
static uintptr_t   s_next_id = 1;

static HrDomain *
hr_find_by_domain(MonoDomain *dom)
{
	if (!s_domains || !dom)
		return NULL;

	GHashTableIter it;
	gpointer k, v;
	g_hash_table_iter_init(&it, s_domains);
	while (g_hash_table_iter_next(&it, &k, &v))
	{
		HrDomain *d = (HrDomain *)v;
		if (d->domain == dom)
			return d;
	}
	return NULL;
}

/* Lookup by simple name in the current domain */
MonoAssembly* mono_hr_try_get_loaded_assembly(const char *simple_name)
{
	if (!simple_name || !s_domains_by_ptr)
		return NULL;

	g_message("hr_fallback: resolving '%s' in domain %p", simple_name, mono_domain_get());

	MonoDomain *cur = mono_domain_get();
	HrDomain *d = (HrDomain *)g_hash_table_lookup(s_domains_by_ptr, cur);
	//HrDomain *d = hr_find_by_domain(mono_domain_get());
	if (!d || !d->assemblies)
		return NULL;

	return (MonoAssembly *)g_hash_table_lookup (d->assemblies, (gpointer)simple_name);
}

static HrDomain *hr_get(gpointer handle_ptr)
{
	if (!s_domains)
		return NULL;
	return (HrDomain *)g_hash_table_lookup(s_domains, handle_ptr);
}

static void hr_domain_free(HrDomain *d)
{
	if (!d)
		return;
	if (d->assemblies)
		g_hash_table_destroy(d->assemblies);
	g_free(d);
}

void *mono_hr_create_domain(const char *name)
{
	if (!s_domains)
	{
		s_domains = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)hr_domain_free);
	}
	if (!s_domains_by_ptr)
		s_domains_by_ptr = g_hash_table_new(g_direct_hash, g_direct_equal);
	

	HrDomain *d = g_new0(HrDomain, 1);
	d->assemblies = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	d->domain = mono_domain_create_appdomain(name ? name : "PluginDomain", NULL);
	if (!d->domain)
	{
		hr_domain_free(d);
		return NULL;
	}

	gpointer handle = (gpointer)(gsize)(s_next_id++);
	g_hash_table_insert(s_domains, handle, d);
	g_hash_table_insert(s_domains_by_ptr, d->domain, d);
	return handle;
}

int mono_hr_unload_domain(void *handle)
{
	HrDomain *d = hr_get(handle);
	if (!d)
		return 0;
	if (s_domains_by_ptr && d && d->domain)
		g_hash_table_remove(s_domains_by_ptr, d->domain);

	mono_domain_unload(d->domain);
	d->domain = NULL;
	g_hash_table_remove(s_domains, handle);
	return 1;
}

MonoReflectionAssembly *ves_icall_mono_hr_load_plugin(void *handle, MonoArray *data_arr)
{
	HrDomain *d = hr_get(handle);
	if (!d || !data_arr)
		return NULL;

	guint32 len = mono_array_length(data_arr);
	if (len == 0)
		return NULL;

	// Pin the managed byte[] so the pointer stays valid while Mono copies it
	guint32 gch = mono_gchandle_new((MonoObject *)data_arr, /*pinned*/ TRUE);
	char *buf = (char *)mono_array_addr_with_size(data_arr, 1, 0);

	MonoDomain *old = mono_domain_get();
	mono_domain_set(d->domain, FALSE);

	MonoImageOpenStatus status = MONO_IMAGE_OK;
	MonoImage *img = mono_image_open_from_data_full(buf, len, TRUE, &status, FALSE);

	// Safe to unpin now: Mono copied the data
	mono_gchandle_free(gch);

	if (!img || status != MONO_IMAGE_OK)
	{
		g_warning("hr_load: mono_image_open_from_data_full failed, status=%d", status);
		mono_domain_set(old, FALSE);
		return NULL;
	}

	MonoAssembly *ass = mono_assembly_load_from_full(img, "", &status, FALSE);
	if (!ass)
	{
		g_warning("hr_load: mono_assembly_load_from_full failed, status=%d", status);
		mono_domain_set(old, FALSE);
		return NULL;
	}

	const char *name = ass->aname.name;
	if (name)
		g_hash_table_replace(d->assemblies, g_strdup(name), ass);

	MonoReflectionAssembly *ref_ass = mono_assembly_get_object(d->domain, ass);
	mono_domain_set(old, FALSE);
	return ref_ass;
}