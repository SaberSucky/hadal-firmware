#pragma once
/*
 * mJS engine symbols, exported to JS module plugins by the JS app itself.
 * The engine lives in the JS app (not in the firmware) to save internal flash.
 * Generated from the former firmware API table entries.
 */
#include <mjs_core_public.h>
#include <mjs_exec_public.h>
#include <mjs_object_public.h>
#include <mjs_string_public.h>
#include <mjs_array_public.h>
#include <mjs_primitive_public.h>
#include <mjs_util_public.h>
#include <mjs_array_buf_public.h>
#include <mjs_ffi_public.h>

#define MJS_API_METHODS \
    API_METHOD(mjs_apply, mjs_err_t, (mjs*, mjs_val_t*, mjs_val_t, mjs_val_t, int, mjs_val_t*)), \
    API_METHOD(mjs_arg, mjs_val_t, (mjs*, int)), \
    API_METHOD(mjs_array_buf_get_ptr, char*, (mjs*, mjs_val_t, size_t*)), \
    API_METHOD(mjs_array_del, void, (mjs*, mjs_val_t, unsigned long)), \
    API_METHOD(mjs_array_get, mjs_val_t, (mjs*, mjs_val_t, unsigned long)), \
    API_METHOD(mjs_array_length, unsigned long, (mjs*, mjs_val_t)), \
    API_METHOD(mjs_array_push, mjs_err_t, (mjs*, mjs_val_t, mjs_val_t)), \
    API_METHOD(mjs_array_set, mjs_err_t, (mjs*, mjs_val_t, unsigned long, mjs_val_t)), \
    API_METHOD(mjs_call, mjs_err_t, (mjs*, mjs_val_t*, mjs_val_t, mjs_val_t, int, ...)), \
    API_METHOD(mjs_create, mjs*, (void*)), \
    API_METHOD(mjs_dataview_get_buf, mjs_val_t, (mjs*, mjs_val_t)), \
    API_METHOD(mjs_del, int, (mjs*, mjs_val_t, const char*, size_t)), \
    API_METHOD(mjs_destroy, void, (mjs*)), \
    API_METHOD(mjs_disown, int, (mjs*, mjs_val_t*)), \
    API_METHOD(mjs_exec, mjs_err_t, (mjs*, const char*, mjs_val_t*)), \
    API_METHOD(mjs_exec_file, mjs_err_t, (mjs*, const char*, mjs_val_t*)), \
    API_METHOD(mjs_exit, void, (mjs*)), \
    API_METHOD(mjs_ffi_resolve, void*, (mjs*, const char*)), \
    API_METHOD(mjs_get, mjs_val_t, (mjs*, mjs_val_t, const char*, size_t)), \
    API_METHOD(mjs_get_bool, int, (mjs*, mjs_val_t)), \
    API_METHOD(mjs_get_context, void*, (mjs*)), \
    API_METHOD(mjs_get_cstring, const char*, (mjs*, mjs_val_t*)), \
    API_METHOD(mjs_get_double, double, (mjs*, mjs_val_t)), \
    API_METHOD(mjs_get_global, mjs_val_t, (mjs*)), \
    API_METHOD(mjs_get_int, int, (mjs*, mjs_val_t)), \
    API_METHOD(mjs_get_int32, int32_t, (mjs*, mjs_val_t)), \
    API_METHOD(mjs_get_lineno_by_offset, int, (mjs*, int)), \
    API_METHOD(mjs_get_offset_by_call_frame_num, int, (mjs*, int)), \
    API_METHOD(mjs_get_ptr, void*, (mjs*, mjs_val_t)), \
    API_METHOD(mjs_get_stack_trace, const char*, (mjs*)), \
    API_METHOD(mjs_get_string, const char*, (mjs*, mjs_val_t*, size_t*)), \
    API_METHOD(mjs_get_this, mjs_val_t, (mjs*)), \
    API_METHOD(mjs_get_v, mjs_val_t, (mjs*, mjs_val_t, mjs_val_t)), \
    API_METHOD(mjs_get_v_proto, mjs_val_t, (mjs*, mjs_val_t, mjs_val_t)), \
    API_METHOD(mjs_is_array, int, (mjs_val_t)), \
    API_METHOD(mjs_is_array_buf, int, (mjs_val_t)), \
    API_METHOD(mjs_is_boolean, int, (mjs_val_t)), \
    API_METHOD(mjs_is_data_view, int, (mjs_val_t)), \
    API_METHOD(mjs_is_foreign, int, (mjs_val_t)), \
    API_METHOD(mjs_is_function, int, (mjs_val_t)), \
    API_METHOD(mjs_is_null, int, (mjs_val_t)), \
    API_METHOD(mjs_is_number, int, (mjs_val_t)), \
    API_METHOD(mjs_is_object, int, (mjs_val_t)), \
    API_METHOD(mjs_is_object_based, int, (mjs_val_t)), \
    API_METHOD(mjs_is_string, int, (mjs_val_t)), \
    API_METHOD(mjs_is_truthy, int, (mjs*, mjs_val_t)), \
    API_METHOD(mjs_is_typed_array, int, (mjs_val_t)), \
    API_METHOD(mjs_is_undefined, int, (mjs_val_t)), \
    API_METHOD(mjs_mk_array, mjs_val_t, (mjs*)), \
    API_METHOD(mjs_mk_array_buf, mjs_val_t, (mjs*, char*, size_t)), \
    API_METHOD(mjs_mk_boolean, mjs_val_t, (mjs*, int)), \
    API_METHOD(mjs_mk_foreign, mjs_val_t, (mjs*, void*)), \
    API_METHOD(mjs_mk_foreign_func, mjs_val_t, (mjs*, mjs_func_ptr_t)), \
    API_METHOD(mjs_mk_function, mjs_val_t, (mjs*, size_t)), \
    API_METHOD(mjs_mk_null, mjs_val_t, (void)), \
    API_METHOD(mjs_mk_number, mjs_val_t, (mjs*, double)), \
    API_METHOD(mjs_mk_object, mjs_val_t, (mjs*)), \
    API_METHOD(mjs_mk_string, mjs_val_t, (mjs*, const char*, size_t, int)), \
    API_METHOD(mjs_mk_undefined, mjs_val_t, (void)), \
    API_METHOD(mjs_nargs, int, (mjs*)), \
    API_METHOD(mjs_next, mjs_val_t, (mjs*, mjs_val_t, mjs_val_t*)), \
    API_METHOD(mjs_own, void, (mjs*, mjs_val_t*)), \
    API_METHOD(mjs_prepend_errorf, mjs_err_t, (mjs*, mjs_err_t, const char*, ...)), \
    API_METHOD(mjs_return, void, (mjs*, mjs_val_t)), \
    API_METHOD(mjs_set, mjs_err_t, (mjs*, mjs_val_t, const char*, size_t, mjs_val_t)), \
    API_METHOD(mjs_set_errorf, mjs_err_t, (mjs*, mjs_err_t, const char*, ...)), \
    API_METHOD(mjs_set_exec_flags_poller, void, (mjs*, mjs_flags_poller_t)), \
    API_METHOD(mjs_set_ffi_resolver, void, (mjs*, mjs_ffi_resolver_t*, void*)), \
    API_METHOD(mjs_set_v, mjs_err_t, (mjs*, mjs_val_t, mjs_val_t, mjs_val_t)), \
    API_METHOD(mjs_sprintf, void, (mjs_val_t, mjs*, char*, size_t)), \
    API_METHOD(mjs_strcmp, int, (mjs*, mjs_val_t*, const char*, size_t)), \
    API_METHOD(mjs_strerror, const char*, (mjs*, mjs_err)), \
    API_METHOD(mjs_struct_to_obj, mjs_val_t, (mjs*, const void*, const mjs_c_struct_member*)), \
    API_METHOD(mjs_to_boolean_v, mjs_val_t, (mjs*, mjs_val_t)), \
    API_METHOD(mjs_to_string, mjs_err_t, (mjs*, mjs_val_t*, char**, size_t*, int*)), \
    API_METHOD(mjs_typeof, const char*, (mjs_val_t))
