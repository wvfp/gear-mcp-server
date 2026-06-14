#include "godot_api.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace gear_mcp {

// ===========================================================================
// Static storage — Phase 1
// ===========================================================================

GDExtensionInterfaceGlobalGetSingleton GodotAPI::s_global_get_singleton = nullptr;
GDExtensionInterfaceObjectCallScriptMethod GodotAPI::s_object_call_script = nullptr;
GDExtensionInterfaceVariantNewNil GodotAPI::s_variant_new_nil = nullptr;
GDExtensionInterfaceVariantDestroy GodotAPI::s_variant_destroy = nullptr;
GDExtensionInterfaceVariantNewCopy GodotAPI::s_variant_new_copy = nullptr;
GDExtensionInterfaceVariantGetType GodotAPI::s_variant_get_type = nullptr;
GDExtensionInterfaceVariantStringify GodotAPI::s_variant_stringify = nullptr;
GDExtensionInterfaceVariantGetObjectInstanceId GodotAPI::s_variant_get_object_instance_id = nullptr;
GDExtensionInterfaceObjectGetInstanceFromId GodotAPI::s_object_get_instance_from_id = nullptr;
GDExtensionInterfaceStringNameNewWithLatin1Chars GodotAPI::s_string_name_new_latin1 = nullptr;
GDExtensionInterfaceStringToUtf8Chars GodotAPI::s_string_to_utf8_chars = nullptr;
GDExtensionInterfaceVariantGetPtrDestructor GodotAPI::s_variant_get_ptr_destructor = nullptr;
GDExtensionInterfaceVariantIterInit GodotAPI::s_variant_iter_init = nullptr;
GDExtensionInterfaceVariantIterNext GodotAPI::s_variant_iter_next = nullptr;
GDExtensionInterfaceVariantIterGet GodotAPI::s_variant_iter_get = nullptr;
GDExtensionInterfaceGetNativeStructSize GodotAPI::s_get_native_struct_size = nullptr;

// ===========================================================================
// Static storage — Phase 2
// ===========================================================================

GearFuncClassdbConstructObject GodotAPI::s_classdb_construct_object = nullptr;
GearFuncClassdbGetMethodBind GodotAPI::s_classdb_get_method_bind = nullptr;
GearFuncObjectMethodBindCall GodotAPI::s_object_method_bind_call = nullptr;
GearFuncObjectDestroy GodotAPI::s_object_destroy = nullptr;

GearFuncVariantBooleanize GodotAPI::s_variant_booleanize = nullptr;

// classdb_get_class_list and classdb_get_parent_classname not available as GDExtension API functions.
// classdb_get_class_list is not in the standard GDExtension interface.
// classdb_get_parent_classname is also not available.

GearFuncStringNewWithUtf8Chars GodotAPI::s_string_new_utf8 = nullptr;
GearFuncStringNewWithLatin1Chars GodotAPI::s_string_new_latin1 = nullptr;

GDExtensionInterfaceGetVariantFromTypeConstructor GodotAPI::s_variant_from_type = nullptr;
GDExtensionInterfaceGetVariantToTypeConstructor GodotAPI::s_variant_to_type = nullptr;
GDExtensionVariantFromTypeConstructorFunc GodotAPI::s_construct_bool = nullptr;
GDExtensionVariantFromTypeConstructorFunc GodotAPI::s_construct_int = nullptr;
GDExtensionVariantFromTypeConstructorFunc GodotAPI::s_construct_float = nullptr;
GDExtensionVariantFromTypeConstructorFunc GodotAPI::s_construct_string = nullptr;
GDExtensionVariantFromTypeConstructorFunc GodotAPI::s_construct_object = nullptr;
GDExtensionTypeFromVariantConstructorFunc GodotAPI::s_extract_int = nullptr;
GDExtensionTypeFromVariantConstructorFunc GodotAPI::s_extract_float = nullptr;
GDExtensionInterfaceVariantSetNamed GodotAPI::s_variant_set_named = nullptr;
GDExtensionInterfaceVariantGetNamed GodotAPI::s_variant_get_named = nullptr;

GDExtensionPtrDestructor GodotAPI::s_string_destructor = nullptr;
GDExtensionPtrDestructor GodotAPI::s_string_name_destructor = nullptr;

bool GodotAPI::s_initialized = false;

// ===========================================================================
// Initialization
// ===========================================================================

void GodotAPI::init(GDExtensionInterfaceGetProcAddress p_get_proc_address) {
    if (!p_get_proc_address) {
        std::fprintf(stderr, "[Gear MCP] GodotAPI::init called with null get_proc_address\n");
        return;
    }

    // Phase 1 function pointers
    s_global_get_singleton = (GDExtensionInterfaceGlobalGetSingleton)p_get_proc_address("global_get_singleton");
    s_object_call_script = (GDExtensionInterfaceObjectCallScriptMethod)p_get_proc_address("object_call_script_method");
    s_variant_new_nil = (GDExtensionInterfaceVariantNewNil)p_get_proc_address("variant_new_nil");
    s_variant_destroy = (GDExtensionInterfaceVariantDestroy)p_get_proc_address("variant_destroy");
    s_variant_new_copy = (GDExtensionInterfaceVariantNewCopy)p_get_proc_address("variant_new_copy");
    s_variant_get_type = (GDExtensionInterfaceVariantGetType)p_get_proc_address("variant_get_type");
    s_variant_stringify = (GDExtensionInterfaceVariantStringify)p_get_proc_address("variant_stringify");
    s_variant_get_object_instance_id = (GDExtensionInterfaceVariantGetObjectInstanceId)p_get_proc_address("variant_get_object_instance_id");
    s_object_get_instance_from_id = (GDExtensionInterfaceObjectGetInstanceFromId)p_get_proc_address("object_get_instance_from_id");
    s_string_name_new_latin1 = (GDExtensionInterfaceStringNameNewWithLatin1Chars)p_get_proc_address("string_name_new_with_latin1_chars");
    s_string_to_utf8_chars = (GDExtensionInterfaceStringToUtf8Chars)p_get_proc_address("string_to_utf8_chars");
    s_variant_get_ptr_destructor = (GDExtensionInterfaceVariantGetPtrDestructor)p_get_proc_address("variant_get_ptr_destructor");
    s_variant_iter_init = (GDExtensionInterfaceVariantIterInit)p_get_proc_address("variant_iter_init");
    s_variant_iter_next = (GDExtensionInterfaceVariantIterNext)p_get_proc_address("variant_iter_next");
    s_variant_iter_get = (GDExtensionInterfaceVariantIterGet)p_get_proc_address("variant_iter_get");
    s_get_native_struct_size = (GDExtensionInterfaceGetNativeStructSize)p_get_proc_address("get_native_struct_size");

    // Phase 2: object creation & method binding
    s_classdb_construct_object = (GearFuncClassdbConstructObject)p_get_proc_address("classdb_construct_object");
    s_classdb_get_method_bind = (GearFuncClassdbGetMethodBind)p_get_proc_address("classdb_get_method_bind");
    s_object_method_bind_call = (GearFuncObjectMethodBindCall)p_get_proc_address("object_method_bind_call");
    s_object_destroy = (GearFuncObjectDestroy)p_get_proc_address("object_destroy");

    // Phase 2: variant type constructors
    s_variant_from_type = (GDExtensionInterfaceGetVariantFromTypeConstructor)p_get_proc_address("get_variant_from_type_constructor");
    s_variant_to_type = (GDExtensionInterfaceGetVariantToTypeConstructor)p_get_proc_address("get_variant_to_type_constructor");
    if (s_variant_from_type) {
        s_construct_bool = s_variant_from_type(GDEXTENSION_VARIANT_TYPE_BOOL);
        s_construct_int = s_variant_from_type(GDEXTENSION_VARIANT_TYPE_INT);
        s_construct_float = s_variant_from_type(GDEXTENSION_VARIANT_TYPE_FLOAT);
        s_construct_string = s_variant_from_type(GDEXTENSION_VARIANT_TYPE_STRING);
        s_construct_object = s_variant_from_type(GDEXTENSION_VARIANT_TYPE_OBJECT);
    }
    if (s_variant_to_type) {
        s_extract_int = s_variant_to_type(GDEXTENSION_VARIANT_TYPE_INT);
        s_extract_float = s_variant_to_type(GDEXTENSION_VARIANT_TYPE_FLOAT);
    }

    // Phase 2: named property access via variant_set_named / variant_get_named
    s_variant_set_named = (GDExtensionInterfaceVariantSetNamed)p_get_proc_address("variant_set_named");
    s_variant_get_named = (GDExtensionInterfaceVariantGetNamed)p_get_proc_address("variant_get_named");

    // Phase 2: variant booleanize
    s_variant_booleanize = (GearFuncVariantBooleanize)p_get_proc_address("variant_booleanize");

    // Phase 2: String constructors
    s_string_new_utf8 = (GearFuncStringNewWithUtf8Chars)p_get_proc_address("string_new_with_utf8_chars");
    s_string_new_latin1 = (GearFuncStringNewWithLatin1Chars)p_get_proc_address("string_new_with_latin1_chars");

    if (s_variant_get_ptr_destructor) {
        s_string_destructor = s_variant_get_ptr_destructor(GDEXTENSION_VARIANT_TYPE_STRING);
        s_string_name_destructor = s_variant_get_ptr_destructor(GDEXTENSION_VARIANT_TYPE_STRING_NAME);
    }

    // Count resolved function pointers
    int count = 0;
    void *ptrs[] = {
        (void *)s_global_get_singleton, (void *)s_object_call_script,
        (void *)s_variant_new_nil, (void *)s_variant_destroy,
        (void *)s_variant_new_copy, (void *)s_variant_get_type,
        (void *)s_variant_stringify, (void *)s_variant_get_object_instance_id,
        (void *)s_object_get_instance_from_id, (void *)s_string_name_new_latin1,
        (void *)s_string_to_utf8_chars, (void *)s_variant_get_ptr_destructor,
        (void *)s_variant_iter_init, (void *)s_variant_iter_next,
        (void *)s_variant_iter_get, (void *)s_get_native_struct_size,
        (void *)s_classdb_construct_object, (void *)s_classdb_get_method_bind,
        (void *)s_object_method_bind_call, (void *)s_object_destroy,
        (void *)s_variant_from_type, (void *)s_variant_to_type,
        (void *)s_construct_bool, (void *)s_construct_int,
        (void *)s_construct_float, (void *)s_construct_string,
        (void *)s_construct_object, (void *)s_extract_int,
        (void *)s_extract_float, (void *)s_variant_booleanize,
        (void *)s_variant_set_named, (void *)s_variant_get_named,
        (void *)s_string_new_utf8, (void *)s_string_new_latin1,
    };
    for (void *p : ptrs) { if (p) count++; }

    s_initialized = true;
    std::printf("[Gear MCP] GodotAPI initialized (%d/%d function pointers resolved)\n", count, (int)(sizeof(ptrs)/sizeof(ptrs[0])));
}

// ===========================================================================
// Internal helpers
// ===========================================================================

void GodotAPI::make_string_name(uint8_t p_buf[8], const char *p_name) {
    if (s_string_name_new_latin1) {
        s_string_name_new_latin1(p_buf, p_name, false);
    }
}

std::string GodotAPI::variant_to_string(GDExtensionConstVariantPtr p_variant) {
    if (!p_variant || !s_variant_stringify || !s_string_to_utf8_chars || !s_string_destructor) {
        return {};
    }

    uint8_t str_buf[VARIANT_BUF_SIZE] = {0};
    s_variant_stringify(p_variant, (GDExtensionStringPtr)str_buf);

    GDExtensionInt len = s_string_to_utf8_chars((GDExtensionConstStringPtr)str_buf, nullptr, 0);

    std::string result;
    if (len > 0) {
        std::vector<char> buf((size_t)(len + 1));
        s_string_to_utf8_chars((GDExtensionConstStringPtr)str_buf, buf.data(), len);
        buf[len] = '\0';
        result = std::string(buf.data());
    }

    if (s_string_destructor) {
        s_string_destructor((GDExtensionStringPtr)str_buf);
    }
    return result;
}

GDExtensionObjectPtr GodotAPI::variant_to_object(GDExtensionConstVariantPtr p_variant) {
    if (!p_variant || !s_variant_get_object_instance_id || !s_object_get_instance_from_id) {
        return nullptr;
    }
    GDObjectInstanceID id = s_variant_get_object_instance_id(p_variant);
    if (id == 0) return nullptr;
    return s_object_get_instance_from_id(id);
}

// ===========================================================================
// Variant construction helpers
// ===========================================================================

void GodotAPI::make_variant_nil(GDExtensionUninitializedVariantPtr r_variant) {
    if (s_variant_new_nil) s_variant_new_nil(r_variant);
}

void GodotAPI::make_variant_bool(GDExtensionUninitializedVariantPtr r_variant, bool p_value) {
    if (s_construct_bool) {
        GDExtensionBool val = p_value ? 1 : 0;
        s_construct_bool(r_variant, (GDExtensionTypePtr)&val);
    } else if (s_variant_new_nil) {
        s_variant_new_nil(r_variant);
    }
}

void GodotAPI::make_variant_int(GDExtensionUninitializedVariantPtr r_variant, int64_t p_value) {
    if (s_construct_int) {
        GDExtensionInt val = (GDExtensionInt)p_value;
        s_construct_int(r_variant, (GDExtensionTypePtr)&val);
    } else if (s_variant_new_nil) {
        s_variant_new_nil(r_variant);
    }
}

void GodotAPI::make_variant_float(GDExtensionUninitializedVariantPtr r_variant, double p_value) {
    if (s_construct_float) {
        s_construct_float(r_variant, (GDExtensionTypePtr)&p_value);
    } else if (s_variant_new_nil) {
        s_variant_new_nil(r_variant);
    }
}

void GodotAPI::make_variant_string(GDExtensionUninitializedVariantPtr r_variant, const std::string &p_value) {
    if (s_construct_string && s_string_new_utf8) {
        uint8_t str_buf[VARIANT_BUF_SIZE] = {0};
        s_string_new_utf8((GDExtensionUninitializedStringPtr)str_buf, p_value.c_str(), (GDExtensionInt)p_value.size());
        s_construct_string(r_variant, (GDExtensionTypePtr)str_buf);
        if (s_string_destructor) s_string_destructor((GDExtensionStringPtr)str_buf);
    } else if (s_variant_new_nil) {
        s_variant_new_nil(r_variant);
    }
}

void GodotAPI::make_variant_object(GDExtensionUninitializedVariantPtr r_variant, GDExtensionObjectPtr p_obj) {
    if (s_construct_object) {
        s_construct_object(r_variant, (GDExtensionTypePtr)&p_obj);
    } else if (s_variant_new_nil) {
        s_variant_new_nil(r_variant);
    }
}

void GodotAPI::destroy_variant(GDExtensionVariantPtr p_variant) {
    if (s_variant_destroy && p_variant) s_variant_destroy(p_variant);
}

GDExtensionVariantType GodotAPI::get_variant_type(GDExtensionConstVariantPtr p_variant) {
    if (s_variant_get_type && p_variant) return s_variant_get_type(p_variant);
    return GDEXTENSION_VARIANT_TYPE_NIL;
}

int64_t GodotAPI::variant_to_int(GDExtensionConstVariantPtr p_variant) {
    if (!p_variant || !s_extract_int) return 0;
    GDExtensionInt result = 0;
    s_extract_int((GDExtensionUninitializedTypePtr)&result, (GDExtensionVariantPtr)p_variant);
    return (int64_t)result;
}

double GodotAPI::variant_to_float(GDExtensionConstVariantPtr p_variant) {
    if (!p_variant || !s_extract_float) return 0.0;
    double result = 0.0;
    s_extract_float((GDExtensionUninitializedTypePtr)&result, (GDExtensionVariantPtr)p_variant);
    return result;
}

bool GodotAPI::variant_to_bool(GDExtensionConstVariantPtr p_variant) {
    if (!p_variant || !s_variant_booleanize) return false;
    GDExtensionBool result = false;
    s_variant_booleanize(p_variant, &result);
    return result != 0;
}

// ===========================================================================
// Method call helpers
// ===========================================================================

std::string GodotAPI::call_method_string(GDExtensionObjectPtr p_obj, const char *p_method) {
    if (!p_obj || !p_method || !s_object_call_script || !s_string_name_new_latin1 || !s_variant_new_nil || !s_variant_destroy) {
        return {};
    }

    uint8_t method_sn[8];
    make_string_name(method_sn, p_method);

    uint8_t ret_buf[VARIANT_BUF_SIZE] = {0};
    s_variant_new_nil((GDExtensionUninitializedVariantPtr)ret_buf);

    GDExtensionCallError error;
    s_object_call_script(p_obj, (GDExtensionConstStringNamePtr)method_sn,
                          nullptr, 0, (GDExtensionUninitializedVariantPtr)ret_buf, &error);

    std::string result = variant_to_string((GDExtensionConstVariantPtr)ret_buf);

    if (s_string_name_destructor) s_string_name_destructor((GDExtensionStringNamePtr)method_sn);
    s_variant_destroy((GDExtensionVariantPtr)ret_buf);

    return result;
}

GDExtensionObjectPtr GodotAPI::call_method_object(GDExtensionObjectPtr p_obj, const char *p_method) {
    if (!p_obj || !p_method || !s_object_call_script || !s_string_name_new_latin1 || !s_variant_new_nil || !s_variant_destroy) {
        return nullptr;
    }

    uint8_t method_sn[8];
    make_string_name(method_sn, p_method);

    uint8_t ret_buf[VARIANT_BUF_SIZE] = {0};
    s_variant_new_nil((GDExtensionUninitializedVariantPtr)ret_buf);

    GDExtensionCallError error;
    s_object_call_script(p_obj, (GDExtensionConstStringNamePtr)method_sn,
                          nullptr, 0, (GDExtensionUninitializedVariantPtr)ret_buf, &error);

    GDExtensionObjectPtr result = variant_to_object((GDExtensionConstVariantPtr)ret_buf);

    if (s_string_name_destructor) s_string_name_destructor((GDExtensionStringNamePtr)method_sn);
    s_variant_destroy((GDExtensionVariantPtr)ret_buf);

    return result;
}

void GodotAPI::call_method_void(GDExtensionObjectPtr p_obj, const char *p_method,
                                  const GDExtensionConstVariantPtr *p_args, int p_arg_count) {
    if (!p_obj || !p_method || !s_object_call_script || !s_string_name_new_latin1) return;

    uint8_t method_sn[8];
    make_string_name(method_sn, p_method);

    uint8_t ret_buf[VARIANT_BUF_SIZE] = {0};
    if (s_variant_new_nil) s_variant_new_nil((GDExtensionUninitializedVariantPtr)ret_buf);

    GDExtensionCallError error;
    s_object_call_script(p_obj, (GDExtensionConstStringNamePtr)method_sn,
                          p_args, p_arg_count, (GDExtensionUninitializedVariantPtr)ret_buf, &error);

    if (s_string_name_destructor) s_string_name_destructor((GDExtensionStringNamePtr)method_sn);
    if (s_variant_destroy) s_variant_destroy((GDExtensionVariantPtr)ret_buf);
}

std::string GodotAPI::call_method_with_args(GDExtensionObjectPtr p_obj, const char *p_method,
                                              const GDExtensionConstVariantPtr *p_args, int p_arg_count) {
    if (!p_obj || !p_method || !s_object_call_script || !s_string_name_new_latin1 || !s_variant_new_nil || !s_variant_destroy) {
        return {};
    }

    uint8_t method_sn[8];
    make_string_name(method_sn, p_method);

    uint8_t ret_buf[VARIANT_BUF_SIZE] = {0};
    s_variant_new_nil((GDExtensionUninitializedVariantPtr)ret_buf);

    GDExtensionCallError error;
    s_object_call_script(p_obj, (GDExtensionConstStringNamePtr)method_sn,
                          p_args, p_arg_count, (GDExtensionUninitializedVariantPtr)ret_buf, &error);

    std::string result = variant_to_string((GDExtensionConstVariantPtr)ret_buf);

    if (s_string_name_destructor) s_string_name_destructor((GDExtensionStringNamePtr)method_sn);
    s_variant_destroy((GDExtensionVariantPtr)ret_buf);

    return result;
}

GDExtensionObjectPtr GodotAPI::call_method_object_with_args(GDExtensionObjectPtr p_obj, const char *p_method,
                                                               const GDExtensionConstVariantPtr *p_args, int p_arg_count) {
    if (!p_obj || !p_method || !s_object_call_script || !s_string_name_new_latin1 || !s_variant_new_nil || !s_variant_destroy) {
        return nullptr;
    }

    uint8_t method_sn[8];
    make_string_name(method_sn, p_method);

    uint8_t ret_buf[VARIANT_BUF_SIZE] = {0};
    s_variant_new_nil((GDExtensionUninitializedVariantPtr)ret_buf);

    GDExtensionCallError error;
    s_object_call_script(p_obj, (GDExtensionConstStringNamePtr)method_sn,
                          p_args, p_arg_count, (GDExtensionUninitializedVariantPtr)ret_buf, &error);

    GDExtensionObjectPtr result = variant_to_object((GDExtensionConstVariantPtr)ret_buf);

    if (s_string_name_destructor) s_string_name_destructor((GDExtensionStringNamePtr)method_sn);
    s_variant_destroy((GDExtensionVariantPtr)ret_buf);

    return result;
}

int GodotAPI::call_method_int(GDExtensionObjectPtr p_obj, const char *p_method) {
    return call_method_int_with_args(p_obj, p_method, nullptr, 0);
}

double GodotAPI::call_method_float(GDExtensionObjectPtr p_obj, const char *p_method) {
    return call_method_float_with_args(p_obj, p_method, nullptr, 0);
}

bool GodotAPI::call_method_bool(GDExtensionObjectPtr p_obj, const char *p_method) {
    return call_method_bool_with_args(p_obj, p_method, nullptr, 0);
}

int GodotAPI::call_method_int_with_args(GDExtensionObjectPtr p_obj, const char *p_method,
                                          const GDExtensionConstVariantPtr *p_args, int p_arg_count) {
    if (!p_obj || !p_method || !s_object_call_script || !s_string_name_new_latin1 || !s_variant_new_nil || !s_variant_destroy) {
        return 0;
    }

    uint8_t method_sn[8];
    make_string_name(method_sn, p_method);

    uint8_t ret_buf[VARIANT_BUF_SIZE] = {0};
    s_variant_new_nil((GDExtensionUninitializedVariantPtr)ret_buf);

    GDExtensionCallError error;
    s_object_call_script(p_obj, (GDExtensionConstStringNamePtr)method_sn,
                          p_args, p_arg_count, (GDExtensionUninitializedVariantPtr)ret_buf, &error);

    int64_t result = variant_to_int((GDExtensionConstVariantPtr)ret_buf);

    if (s_string_name_destructor) s_string_name_destructor((GDExtensionStringNamePtr)method_sn);
    s_variant_destroy((GDExtensionVariantPtr)ret_buf);

    return (int)result;
}

double GodotAPI::call_method_float_with_args(GDExtensionObjectPtr p_obj, const char *p_method,
                                                const GDExtensionConstVariantPtr *p_args, int p_arg_count) {
    if (!p_obj || !p_method || !s_object_call_script || !s_string_name_new_latin1 || !s_variant_new_nil || !s_variant_destroy) {
        return 0.0;
    }

    uint8_t method_sn[8];
    make_string_name(method_sn, p_method);

    uint8_t ret_buf[VARIANT_BUF_SIZE] = {0};
    s_variant_new_nil((GDExtensionUninitializedVariantPtr)ret_buf);

    GDExtensionCallError error;
    s_object_call_script(p_obj, (GDExtensionConstStringNamePtr)method_sn,
                          p_args, p_arg_count, (GDExtensionUninitializedVariantPtr)ret_buf, &error);

    double result = variant_to_float((GDExtensionConstVariantPtr)ret_buf);

    if (s_string_name_destructor) s_string_name_destructor((GDExtensionStringNamePtr)method_sn);
    s_variant_destroy((GDExtensionVariantPtr)ret_buf);

    return result;
}

bool GodotAPI::call_method_bool_with_args(GDExtensionObjectPtr p_obj, const char *p_method,
                                            const GDExtensionConstVariantPtr *p_args, int p_arg_count) {
    if (!p_obj || !p_method || !s_object_call_script || !s_string_name_new_latin1 || !s_variant_new_nil || !s_variant_destroy || !s_variant_booleanize) {
        return false;
    }

    uint8_t method_sn[8];
    make_string_name(method_sn, p_method);

    uint8_t ret_buf[VARIANT_BUF_SIZE] = {0};
    s_variant_new_nil((GDExtensionUninitializedVariantPtr)ret_buf);

    GDExtensionCallError error;
    s_object_call_script(p_obj, (GDExtensionConstStringNamePtr)method_sn,
                          p_args, p_arg_count, (GDExtensionUninitializedVariantPtr)ret_buf, &error);

    GDExtensionBool result_b = false;
    s_variant_booleanize((GDExtensionConstVariantPtr)ret_buf, &result_b);

    if (s_string_name_destructor) s_string_name_destructor((GDExtensionStringNamePtr)method_sn);
    s_variant_destroy((GDExtensionVariantPtr)ret_buf);

    return result_b != 0;
}

GDExtensionObjectPtr GodotAPI::get_global_singleton(const char *p_name) {
    if (!p_name || !s_global_get_singleton || !s_string_name_new_latin1) return nullptr;
    uint8_t sn[8];
    make_string_name(sn, p_name);
    GDExtensionObjectPtr obj = s_global_get_singleton((GDExtensionConstStringNamePtr)sn);
    if (s_string_name_destructor) s_string_name_destructor((GDExtensionStringNamePtr)sn);
    return obj;
}

// ===========================================================================
// String helpers
// ===========================================================================

std::string GodotAPI::escape_json(const std::string &p_input) {
    std::string out;
    out.reserve(p_input.size() + 8);
    for (size_t i = 0; i < p_input.size(); i++) {
        char c = p_input[i];
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                    out += buf;
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

// ===========================================================================
// Editor access
// ===========================================================================

GDExtensionObjectPtr GodotAPI::get_editor_interface() {
    if (!s_global_get_singleton || !s_string_name_new_latin1) return nullptr;

    uint8_t editor_sn[8];
    make_string_name(editor_sn, "EditorInterface");
    GDExtensionObjectPtr editor = s_global_get_singleton((GDExtensionConstStringNamePtr)editor_sn);
    if (s_string_name_destructor) s_string_name_destructor((GDExtensionStringNamePtr)editor_sn);
    return editor;
}

GDExtensionObjectPtr GodotAPI::get_edited_scene_root() {
    GDExtensionObjectPtr editor = get_editor_interface();
    if (!editor) return nullptr;
    return call_method_object(editor, "get_edited_scene_root");
}

GDExtensionObjectPtr GodotAPI::get_selection() {
    GDExtensionObjectPtr editor = get_editor_interface();
    if (!editor) return nullptr;
    return call_method_object(editor, "get_selection");
}

std::vector<GDExtensionObjectPtr> GodotAPI::get_selected_nodes() {
    std::vector<GDExtensionObjectPtr> result;
    GDExtensionObjectPtr selection = get_selection();
    if (!selection || !s_variant_new_nil || !s_variant_destroy) return result;

    uint8_t method_sn[8];
    make_string_name(method_sn, "get_selected_nodes");

    uint8_t nodes_variant[VARIANT_BUF_SIZE] = {0};
    s_variant_new_nil((GDExtensionUninitializedVariantPtr)nodes_variant);

    GDExtensionCallError err;
    s_object_call_script(selection, (GDExtensionConstStringNamePtr)method_sn,
                          nullptr, 0, (GDExtensionUninitializedVariantPtr)nodes_variant, &err);

    if (s_string_name_destructor) s_string_name_destructor((GDExtensionStringNamePtr)method_sn);

    if (s_variant_get_type && s_variant_get_type((GDExtensionConstVariantPtr)nodes_variant) == GDEXTENSION_VARIANT_TYPE_ARRAY) {
        uint8_t iter_variant[VARIANT_BUF_SIZE] = {0};
        uint8_t elem_variant[VARIANT_BUF_SIZE] = {0};

        GDExtensionBool valid = false;
        bool has_more = s_variant_iter_init &&
            s_variant_iter_init((GDExtensionConstVariantPtr)nodes_variant,
                                (GDExtensionUninitializedVariantPtr)iter_variant, &valid);

        while (has_more && valid) {
            if (s_variant_iter_get) {
                s_variant_new_nil((GDExtensionUninitializedVariantPtr)elem_variant);
                s_variant_iter_get((GDExtensionConstVariantPtr)nodes_variant,
                                   (GDExtensionVariantPtr)iter_variant,
                                   (GDExtensionUninitializedVariantPtr)elem_variant, &valid);

                GDExtensionObjectPtr node_obj = variant_to_object((GDExtensionConstVariantPtr)elem_variant);
                if (node_obj) result.push_back(node_obj);
                s_variant_destroy((GDExtensionVariantPtr)elem_variant);
            }
            if (s_variant_iter_next) {
                has_more = s_variant_iter_next((GDExtensionConstVariantPtr)nodes_variant,
                                               (GDExtensionVariantPtr)iter_variant, &valid);
            } else {
                break;
            }
        }
        s_variant_destroy((GDExtensionVariantPtr)iter_variant);
    }
    s_variant_destroy((GDExtensionVariantPtr)nodes_variant);

    return result;
}

GDExtensionObjectPtr GodotAPI::get_editor_undo_redo() {
    GDExtensionObjectPtr editor = get_editor_interface();
    if (!editor) return nullptr;
    return call_method_object(editor, "get_editor_undo_redo");
}

std::string GodotAPI::get_project_path() {
    char cwd_buf[4096];
#ifdef _MSC_VER
    if (_getcwd(cwd_buf, sizeof(cwd_buf)) != nullptr) {
#else
    if (getcwd(cwd_buf, sizeof(cwd_buf)) != nullptr) {
#endif
        return std::string(cwd_buf);
    }
    return {};
}

std::string GodotAPI::res_to_absolute(const std::string &p_res_path) {
    if (p_res_path.size() >= 6 && p_res_path.substr(0, 6) == "res://") {
        return get_project_path() + "/" + p_res_path.substr(6);
    }
    return p_res_path;
}

std::string GodotAPI::absolute_to_res(const std::string &p_abs_path) {
    std::string project = get_project_path();
    if (!project.empty() && p_abs_path.size() > project.size() &&
        p_abs_path.substr(0, project.size()) == project) {
        return "res://" + p_abs_path.substr(project.size() + 1);
    }
    return p_abs_path;
}

// ===========================================================================
// Engine info
// ===========================================================================

std::string GodotAPI::get_godot_version() {
    // Try Engine singleton
    if (!s_global_get_singleton || !s_string_name_new_latin1) return "Godot Engine v4.x";

    uint8_t engine_sn[8];
    make_string_name(engine_sn, "Engine");
    GDExtensionObjectPtr engine = s_global_get_singleton((GDExtensionConstStringNamePtr)engine_sn);
    if (s_string_name_destructor) s_string_name_destructor((GDExtensionStringNamePtr)engine_sn);

    if (!engine) return "Godot Engine v4.x";

    // Call get_version_info() which returns a Dictionary
    // We'll use variant_to_string on the result
    uint8_t method_sn[8];
    make_string_name(method_sn, "get_version_info");

    uint8_t ret_buf[VARIANT_BUF_SIZE] = {0};
    if (s_variant_new_nil) s_variant_new_nil((GDExtensionUninitializedVariantPtr)ret_buf);

    GDExtensionCallError error;
    if (s_object_call_script) {
        s_object_call_script(engine, (GDExtensionConstStringNamePtr)method_sn,
                              nullptr, 0, (GDExtensionUninitializedVariantPtr)ret_buf, &error);
    }
    if (s_string_name_destructor) s_string_name_destructor((GDExtensionStringNamePtr)method_sn);

    std::string result = variant_to_string((GDExtensionConstVariantPtr)ret_buf);
    if (s_variant_destroy) s_variant_destroy((GDExtensionVariantPtr)ret_buf);

    if (result.empty()) return "Godot Engine v4.x";
    return result;
}

// ===========================================================================
// OS::get_executable_path — returns the path to the running Godot binary.
// Used by export_project to re-invoke godot --export-release.
// ===========================================================================

std::string GodotAPI::get_executable_path() {
    if (!s_global_get_singleton || !s_string_name_new_latin1) return "";

    uint8_t os_sn[8];
    make_string_name(os_sn, "OS");
    GDExtensionObjectPtr os = s_global_get_singleton((GDExtensionConstStringNamePtr)os_sn);
    if (s_string_name_destructor) s_string_name_destructor((GDExtensionStringNamePtr)os_sn);
    if (!os) return "";

    std::string path = call_method_string(os, "get_executable_path");
    return path;
}

// ===========================================================================
// Node operations
// ===========================================================================

GDExtensionObjectPtr GodotAPI::create_node(const std::string &p_class_name) {
    if (!s_classdb_construct_object || !s_string_name_new_latin1) return nullptr;

    uint8_t class_sn[8];
    make_string_name(class_sn, p_class_name.c_str());

    GDExtensionObjectPtr obj = s_classdb_construct_object((GDExtensionConstStringNamePtr)class_sn);
    if (s_string_name_destructor) s_string_name_destructor((GDExtensionStringNamePtr)class_sn);
    return obj;
}

void GodotAPI::add_child(GDExtensionObjectPtr p_parent, GDExtensionObjectPtr p_child) {
    if (!p_parent || !p_child) return;

    // Create object variant for child
    uint8_t child_variant[VARIANT_BUF_SIZE] = {0};
    make_variant_object((GDExtensionUninitializedVariantPtr)child_variant, p_child);

    GDExtensionConstVariantPtr args[] = { (GDExtensionConstVariantPtr)child_variant };
    call_method_void(p_parent, "add_child", args, 1);

    destroy_variant((GDExtensionVariantPtr)child_variant);
}

void GodotAPI::remove_child(GDExtensionObjectPtr p_parent, GDExtensionObjectPtr p_child) {
    if (!p_parent || !p_child) return;

    uint8_t child_variant[VARIANT_BUF_SIZE] = {0};
    make_variant_object((GDExtensionUninitializedVariantPtr)child_variant, p_child);

    GDExtensionConstVariantPtr args[] = { (GDExtensionConstVariantPtr)child_variant };
    call_method_void(p_parent, "remove_child", args, 1);

    destroy_variant((GDExtensionVariantPtr)child_variant);
}

void GodotAPI::set_owner(GDExtensionObjectPtr p_node, GDExtensionObjectPtr p_owner) {
    if (!p_node) return;

    uint8_t owner_variant[VARIANT_BUF_SIZE] = {0};
    make_variant_object((GDExtensionUninitializedVariantPtr)owner_variant, p_owner);

    GDExtensionConstVariantPtr args[] = { (GDExtensionConstVariantPtr)owner_variant };
    call_method_void(p_node, "set_owner", args, 1);

    destroy_variant((GDExtensionVariantPtr)owner_variant);
}

void GodotAPI::set_owner_recursive(GDExtensionObjectPtr p_node, GDExtensionObjectPtr p_owner) {
    if (!p_node) return;
    set_owner(p_node, p_owner);

    // Recurse into children
    int count = get_child_count(p_node);
    for (int i = 0; i < count; i++) {
        // Get child at index i
        uint8_t idx_variant[VARIANT_BUF_SIZE] = {0};
        make_variant_int((GDExtensionUninitializedVariantPtr)idx_variant, i);

        GDExtensionConstVariantPtr args[] = { (GDExtensionConstVariantPtr)idx_variant };
        GDExtensionObjectPtr child = call_method_object_with_args(p_node, "get_child", args, 1);

        destroy_variant((GDExtensionVariantPtr)idx_variant);

        if (child) {
            set_owner_recursive(child, p_owner);
        }
    }
}

std::string GodotAPI::get_node_name(GDExtensionObjectPtr p_node) {
    return call_method_string(p_node, "get_name");
}

std::string GodotAPI::get_node_class(GDExtensionObjectPtr p_node) {
    return call_method_string(p_node, "get_class");
}

GDExtensionObjectPtr GodotAPI::get_node_child(GDExtensionObjectPtr p_node, const std::string &p_path) {
    if (!p_node || p_path.empty()) return nullptr;

    // Use get_node_or_null(path) for NodePath resolution
    // First create a NodePath variant from the string
    // For simplicity, use call_method_string approach
    // Actually, get_node expects a NodePath, but call_method_script should auto-convert

    // We need to pass the path as a String argument
    // Since we can't easily create String variants yet, use get_child for simple paths
    // and get_node for complex paths

    if (p_path == ".") return p_node;

    // Try using object_call_script_method with a string argument
    // The script method should handle the String -> NodePath conversion
    // If that doesn't work, fall back to manual path walking

    // Walk the path manually
    std::string remaining = p_path;
    GDExtensionObjectPtr current = p_node;

    while (!remaining.empty() && current) {
        size_t slash = remaining.find('/');
        std::string segment = (slash == std::string::npos) ? remaining : remaining.substr(0, slash);
        remaining = (slash == std::string::npos) ? "" : remaining.substr(slash + 1);

        if (segment == ".") continue;
        if (segment == "..") {
            current = call_method_object(current, "get_parent");
            continue;
        }

        // Find child by name
        int count = get_child_count(current);
        GDExtensionObjectPtr found = nullptr;
        for (int i = 0; i < count; i++) {
            uint8_t idx_variant[VARIANT_BUF_SIZE] = {0};
            make_variant_int((GDExtensionUninitializedVariantPtr)idx_variant, i);

            GDExtensionConstVariantPtr args[] = { (GDExtensionConstVariantPtr)idx_variant };
            GDExtensionObjectPtr child = call_method_object_with_args(current, "get_child", args, 1);
            destroy_variant((GDExtensionVariantPtr)idx_variant);

            if (child) {
                std::string name = get_node_name(child);
                if (name == segment) {
                    found = child;
                    break;
                }
            }
        }
        current = found;
    }

    return current;
}

int GodotAPI::get_child_count(GDExtensionObjectPtr p_node) {
    if (!p_node) return 0;

    // Call get_child_count() - returns an int
    uint8_t method_sn[8];
    make_string_name(method_sn, "get_child_count");

    uint8_t ret_buf[VARIANT_BUF_SIZE] = {0};
    if (s_variant_new_nil) s_variant_new_nil((GDExtensionUninitializedVariantPtr)ret_buf);

    GDExtensionCallError error;
    if (s_object_call_script) {
        s_object_call_script(p_node, (GDExtensionConstStringNamePtr)method_sn,
                              nullptr, 0, (GDExtensionUninitializedVariantPtr)ret_buf, &error);
    }
    if (s_string_name_destructor) s_string_name_destructor((GDExtensionStringNamePtr)method_sn);

    int64_t result = variant_to_int((GDExtensionConstVariantPtr)ret_buf);
    if (s_variant_destroy) s_variant_destroy((GDExtensionVariantPtr)ret_buf);

    return (int)result;
}

std::vector<GDExtensionObjectPtr> GodotAPI::get_children(GDExtensionObjectPtr p_node) {
    std::vector<GDExtensionObjectPtr> result;
    if (!p_node) return result;

    int count = get_child_count(p_node);
    result.reserve(count);
    for (int i = 0; i < count; i++) {
        uint8_t idx_variant[VARIANT_BUF_SIZE] = {0};
        make_variant_int((GDExtensionUninitializedVariantPtr)idx_variant, i);

        GDExtensionConstVariantPtr args[] = { (GDExtensionConstVariantPtr)idx_variant };
        GDExtensionObjectPtr child = call_method_object_with_args(p_node, "get_child", args, 1);
        destroy_variant((GDExtensionVariantPtr)idx_variant);

        if (child) result.push_back(child);
    }
    return result;
}

GDExtensionObjectPtr GodotAPI::duplicate_node(GDExtensionObjectPtr p_node, int p_flags) {
    if (!p_node) return nullptr;

    uint8_t flags_variant[VARIANT_BUF_SIZE] = {0};
    make_variant_int((GDExtensionUninitializedVariantPtr)flags_variant, p_flags);

    GDExtensionConstVariantPtr args[] = { (GDExtensionConstVariantPtr)flags_variant };
    GDExtensionObjectPtr result = call_method_object_with_args(p_node, "duplicate", args, 1);

    destroy_variant((GDExtensionVariantPtr)flags_variant);
    return result;
}

void GodotAPI::queue_free(GDExtensionObjectPtr p_node) {
    call_method_void(p_node, "queue_free", nullptr, 0);
}

// ===========================================================================
// Property operations
// ===========================================================================

bool GodotAPI::set_property(GDExtensionObjectPtr p_obj, const std::string &p_name,
                              GDExtensionConstVariantPtr p_value) {
    if (!p_obj || !s_variant_set_named || !s_construct_object || !s_string_name_new_latin1) return false;

    // Wrap the object in a variant
    uint8_t obj_variant[VARIANT_BUF_SIZE] = {0};
    s_construct_object((GDExtensionUninitializedVariantPtr)obj_variant, (GDExtensionTypePtr)&p_obj);

    // Create StringName for property name
    uint8_t prop_sn[8];
    s_string_name_new_latin1(prop_sn, p_name.c_str(), false);

    GDExtensionBool valid = false;
    s_variant_set_named((GDExtensionVariantPtr)obj_variant,
                         (GDExtensionConstStringNamePtr)prop_sn,
                         p_value, &valid);

    if (s_string_name_destructor) s_string_name_destructor((GDExtensionStringNamePtr)prop_sn);
    if (s_variant_destroy) s_variant_destroy((GDExtensionVariantPtr)obj_variant);

    return valid != 0;
}

bool GodotAPI::get_property(GDExtensionObjectPtr p_obj, const std::string &p_name,
                              GDExtensionUninitializedVariantPtr r_value) {
    if (!p_obj || !s_variant_get_named || !s_construct_object || !s_string_name_new_latin1) return false;

    // Wrap the object in a variant
    uint8_t obj_variant[VARIANT_BUF_SIZE] = {0};
    s_construct_object((GDExtensionUninitializedVariantPtr)obj_variant, (GDExtensionTypePtr)&p_obj);

    // Create StringName for property name
    uint8_t prop_sn[8];
    s_string_name_new_latin1(prop_sn, p_name.c_str(), false);

    GDExtensionBool valid = false;
    s_variant_get_named((GDExtensionConstVariantPtr)obj_variant,
                         (GDExtensionConstStringNamePtr)prop_sn,
                         r_value, &valid);

    if (s_string_name_destructor) s_string_name_destructor((GDExtensionStringNamePtr)prop_sn);
    if (s_variant_destroy) s_variant_destroy((GDExtensionVariantPtr)obj_variant);

    return valid != 0;
}

std::string GodotAPI::get_property_json(GDExtensionObjectPtr p_obj, const std::string &p_name) {
    if (!p_obj) return "null";

    uint8_t val_buf[VARIANT_BUF_SIZE] = {0};
    if (s_variant_new_nil) s_variant_new_nil((GDExtensionUninitializedVariantPtr)val_buf);

    bool valid = get_property(p_obj, p_name, (GDExtensionUninitializedVariantPtr)val_buf);
    if (!valid) {
        if (s_variant_destroy) s_variant_destroy((GDExtensionVariantPtr)val_buf);
        return "null";
    }

    std::string result = variant_to_string((GDExtensionConstVariantPtr)val_buf);
    if (s_variant_destroy) s_variant_destroy((GDExtensionVariantPtr)val_buf);

    return "\"" + escape_json(result) + "\"";
}

GDExtensionObjectPtr GodotAPI::get_property_object(GDExtensionObjectPtr p_obj, const std::string &p_name) {
    if (!p_obj) return nullptr;

    uint8_t val_buf[VARIANT_BUF_SIZE] = {0};
    if (s_variant_new_nil) s_variant_new_nil((GDExtensionUninitializedVariantPtr)val_buf);

    bool valid = get_property(p_obj, p_name, (GDExtensionUninitializedVariantPtr)val_buf);
    if (!valid) {
        if (s_variant_destroy) s_variant_destroy((GDExtensionVariantPtr)val_buf);
        return nullptr;
    }

    GDExtensionObjectPtr obj = variant_to_object((GDExtensionConstVariantPtr)val_buf);
    if (s_variant_destroy) s_variant_destroy((GDExtensionVariantPtr)val_buf);
    return obj;
}

bool GodotAPI::set_property_from_json(GDExtensionObjectPtr p_obj, const std::string &p_name,
                                        const std::string &p_json) {
    if (!p_obj) return false;

    // Parse JSON and create appropriate variant
    // For Phase 2, handle basic types: string, int, float, bool, null
    // Strip quotes if it's a string
    std::string value_str = p_json;
    // Trim whitespace
    size_t s = value_str.find_first_not_of(" \t\r\n");
    size_t e = value_str.find_last_not_of(" \t\r\n");
    if (s != std::string::npos) value_str = value_str.substr(s, e - s + 1);

    uint8_t val_buf[VARIANT_BUF_SIZE] = {0};

    if (value_str == "null") {
        make_variant_nil((GDExtensionUninitializedVariantPtr)val_buf);
    } else if (value_str == "true") {
        make_variant_bool((GDExtensionUninitializedVariantPtr)val_buf, true);
    } else if (value_str == "false") {
        make_variant_bool((GDExtensionUninitializedVariantPtr)val_buf, false);
    } else if (value_str.size() >= 2 && value_str.front() == '"' && value_str.back() == '"') {
        // String value - strip quotes
        std::string str_val = value_str.substr(1, value_str.size() - 2);
        // Unescape
        std::string unescaped;
        for (size_t i = 0; i < str_val.size(); i++) {
            if (str_val[i] == '\\' && i + 1 < str_val.size()) {
                switch (str_val[i + 1]) {
                    case '"': unescaped += '"'; i++; break;
                    case '\\': unescaped += '\\'; i++; break;
                    case 'n': unescaped += '\n'; i++; break;
                    case 'r': unescaped += '\r'; i++; break;
                    case 't': unescaped += '\t'; i++; break;
                    default: unescaped += str_val[i]; break;
                }
            } else {
                unescaped += str_val[i];
            }
        }
        make_variant_string((GDExtensionUninitializedVariantPtr)val_buf, unescaped);
    } else {
        // Try as number
        bool is_float = value_str.find('.') != std::string::npos;
        if (is_float) {
            make_variant_float((GDExtensionUninitializedVariantPtr)val_buf, std::stod(value_str));
        } else {
            try {
                make_variant_int((GDExtensionUninitializedVariantPtr)val_buf, std::stoll(value_str));
            } catch (...) {
                make_variant_nil((GDExtensionUninitializedVariantPtr)val_buf);
            }
        }
    }

    bool result = set_property(p_obj, p_name, (GDExtensionConstVariantPtr)val_buf);
    if (s_variant_destroy) s_variant_destroy((GDExtensionVariantPtr)val_buf);
    return result;
}

// ===========================================================================
// UndoRedo operations
// ===========================================================================

void GodotAPI::undo_redo_create_action(const std::string &p_name) {
    GDExtensionObjectPtr undo_redo = get_editor_undo_redo();
    if (!undo_redo) return;

    uint8_t name_variant[VARIANT_BUF_SIZE] = {0};
    make_variant_string((GDExtensionUninitializedVariantPtr)name_variant, p_name);

    GDExtensionConstVariantPtr args[] = { (GDExtensionConstVariantPtr)name_variant };
    call_method_void(undo_redo, "create_action", args, 1);

    destroy_variant((GDExtensionVariantPtr)name_variant);
}

void GodotAPI::undo_redo_add_do_method(GDExtensionObjectPtr p_obj, const std::string &p_method,
                                          const GDExtensionConstVariantPtr *p_args, int p_arg_count) {
    GDExtensionObjectPtr undo_redo = get_editor_undo_redo();
    if (!undo_redo || !p_obj) return;

    // add_do_method(object, method, args...)
    uint8_t obj_variant[VARIANT_BUF_SIZE] = {0};
    uint8_t method_variant[VARIANT_BUF_SIZE] = {0};
    make_variant_object((GDExtensionUninitializedVariantPtr)obj_variant, p_obj);
    make_variant_string((GDExtensionUninitializedVariantPtr)method_variant, p_method);

    // Build args array: [object, method, ...extra_args]
    std::vector<uint8_t *> extra_variants;
    std::vector<GDExtensionConstVariantPtr> all_args;
    all_args.push_back((GDExtensionConstVariantPtr)obj_variant);
    all_args.push_back((GDExtensionConstVariantPtr)method_variant);

    for (int i = 0; i < p_arg_count; i++) {
        all_args.push_back(p_args[i]);
    }

    call_method_void(undo_redo, "add_do_method", all_args.data(), (int)all_args.size());

    destroy_variant((GDExtensionVariantPtr)obj_variant);
    destroy_variant((GDExtensionVariantPtr)method_variant);
}

void GodotAPI::undo_redo_add_undo_method(GDExtensionObjectPtr p_obj, const std::string &p_method,
                                            const GDExtensionConstVariantPtr *p_args, int p_arg_count) {
    GDExtensionObjectPtr undo_redo = get_editor_undo_redo();
    if (!undo_redo || !p_obj) return;

    uint8_t obj_variant[VARIANT_BUF_SIZE] = {0};
    uint8_t method_variant[VARIANT_BUF_SIZE] = {0};
    make_variant_object((GDExtensionUninitializedVariantPtr)obj_variant, p_obj);
    make_variant_string((GDExtensionUninitializedVariantPtr)method_variant, p_method);

    std::vector<GDExtensionConstVariantPtr> all_args;
    all_args.push_back((GDExtensionConstVariantPtr)obj_variant);
    all_args.push_back((GDExtensionConstVariantPtr)method_variant);

    for (int i = 0; i < p_arg_count; i++) {
        all_args.push_back(p_args[i]);
    }

    call_method_void(undo_redo, "add_undo_method", all_args.data(), (int)all_args.size());

    destroy_variant((GDExtensionVariantPtr)obj_variant);
    destroy_variant((GDExtensionVariantPtr)method_variant);
}

void GodotAPI::undo_redo_add_do_property(GDExtensionObjectPtr p_obj, const std::string &p_name,
                                            GDExtensionConstVariantPtr p_value) {
    GDExtensionObjectPtr undo_redo = get_editor_undo_redo();
    if (!undo_redo || !p_obj) return;

    uint8_t obj_variant[VARIANT_BUF_SIZE] = {0};
    uint8_t name_variant[VARIANT_BUF_SIZE] = {0};
    make_variant_object((GDExtensionUninitializedVariantPtr)obj_variant, p_obj);
    make_variant_string((GDExtensionUninitializedVariantPtr)name_variant, p_name);

    GDExtensionConstVariantPtr args[] = {
        (GDExtensionConstVariantPtr)obj_variant,
        (GDExtensionConstVariantPtr)name_variant,
        p_value
    };
    call_method_void(undo_redo, "add_do_property", args, 3);

    destroy_variant((GDExtensionVariantPtr)obj_variant);
    destroy_variant((GDExtensionVariantPtr)name_variant);
}

void GodotAPI::undo_redo_add_undo_property(GDExtensionObjectPtr p_obj, const std::string &p_name,
                                              GDExtensionConstVariantPtr p_value) {
    GDExtensionObjectPtr undo_redo = get_editor_undo_redo();
    if (!undo_redo || !p_obj) return;

    uint8_t obj_variant[VARIANT_BUF_SIZE] = {0};
    uint8_t name_variant[VARIANT_BUF_SIZE] = {0};
    make_variant_object((GDExtensionUninitializedVariantPtr)obj_variant, p_obj);
    make_variant_string((GDExtensionUninitializedVariantPtr)name_variant, p_name);

    GDExtensionConstVariantPtr args[] = {
        (GDExtensionConstVariantPtr)obj_variant,
        (GDExtensionConstVariantPtr)name_variant,
        p_value
    };
    call_method_void(undo_redo, "add_undo_property", args, 3);

    destroy_variant((GDExtensionVariantPtr)obj_variant);
    destroy_variant((GDExtensionVariantPtr)name_variant);
}

void GodotAPI::undo_redo_add_do_reference(GDExtensionObjectPtr p_obj) {
    GDExtensionObjectPtr undo_redo = get_editor_undo_redo();
    if (!undo_redo || !p_obj) return;

    uint8_t obj_variant[VARIANT_BUF_SIZE] = {0};
    make_variant_object((GDExtensionUninitializedVariantPtr)obj_variant, p_obj);

    GDExtensionConstVariantPtr args[] = { (GDExtensionConstVariantPtr)obj_variant };
    call_method_void(undo_redo, "add_do_reference", args, 1);

    destroy_variant((GDExtensionVariantPtr)obj_variant);
}

void GodotAPI::undo_redo_add_undo_reference(GDExtensionObjectPtr p_obj) {
    GDExtensionObjectPtr undo_redo = get_editor_undo_redo();
    if (!undo_redo || !p_obj) return;

    uint8_t obj_variant[VARIANT_BUF_SIZE] = {0};
    make_variant_object((GDExtensionUninitializedVariantPtr)obj_variant, p_obj);

    GDExtensionConstVariantPtr args[] = { (GDExtensionConstVariantPtr)obj_variant };
    call_method_void(undo_redo, "add_undo_reference", args, 1);

    destroy_variant((GDExtensionVariantPtr)obj_variant);
}

void GodotAPI::undo_redo_commit_action() {
    GDExtensionObjectPtr undo_redo = get_editor_undo_redo();
    if (!undo_redo) return;
    call_method_void(undo_redo, "commit_action", nullptr, 0);
}

// ===========================================================================
// ClassDB queries — via ClassDB singleton (object_call_script_method)
// ===========================================================================

bool GodotAPI::classdb_class_exists(const std::string &p_class_name) {
    if (!s_global_get_singleton || !s_string_name_new_latin1 || !s_object_call_script || !s_variant_new_nil || !s_variant_destroy) {
        // Fallback: try to construct the class — if non-null, it exists
        if (s_classdb_construct_object) {
            uint8_t class_sn[8];
            make_string_name(class_sn, p_class_name.c_str());
            GDExtensionObjectPtr obj = s_classdb_construct_object((GDExtensionConstStringNamePtr)class_sn);
            if (s_string_name_destructor) s_string_name_destructor((GDExtensionStringNamePtr)class_sn);
            if (obj) {
                s_object_destroy(obj);
                return true;
            }
            return false;
        }
        return false;
    }

    uint8_t classdb_sn[8];
    make_string_name(classdb_sn, "ClassDB");
    GDExtensionObjectPtr classdb = s_global_get_singleton((GDExtensionConstStringNamePtr)classdb_sn);
    if (s_string_name_destructor) s_string_name_destructor((GDExtensionStringNamePtr)classdb_sn);
    if (!classdb) return false;

    uint8_t class_name_variant[VARIANT_BUF_SIZE] = {0};
    make_variant_string((GDExtensionUninitializedVariantPtr)class_name_variant, p_class_name);

    GDExtensionConstVariantPtr args[] = { (GDExtensionConstVariantPtr)class_name_variant };

    uint8_t method_sn[8];
    make_string_name(method_sn, "class_exists");

    uint8_t ret_buf[VARIANT_BUF_SIZE] = {0};
    s_variant_new_nil((GDExtensionUninitializedVariantPtr)ret_buf);

    GDExtensionCallError error;
    s_object_call_script(classdb, (GDExtensionConstStringNamePtr)method_sn,
                          args, 1, (GDExtensionUninitializedVariantPtr)ret_buf, &error);

    if (s_string_name_destructor) s_string_name_destructor((GDExtensionStringNamePtr)method_sn);

    bool result = variant_to_bool((GDExtensionConstVariantPtr)ret_buf);
    s_variant_destroy((GDExtensionVariantPtr)ret_buf);
    destroy_variant((GDExtensionVariantPtr)class_name_variant);

    return result;
}

std::vector<std::string> GodotAPI::classdb_get_class_list() {
    std::vector<std::string> result;
    if (!s_global_get_singleton || !s_string_name_new_latin1 || !s_object_call_script || !s_variant_new_nil || !s_variant_destroy) {
        return result;
    }

    uint8_t classdb_sn[8];
    make_string_name(classdb_sn, "ClassDB");
    GDExtensionObjectPtr classdb = s_global_get_singleton((GDExtensionConstStringNamePtr)classdb_sn);
    if (s_string_name_destructor) s_string_name_destructor((GDExtensionStringNamePtr)classdb_sn);
    if (!classdb) return result;

    uint8_t method_sn[8];
    make_string_name(method_sn, "get_class_list");

    uint8_t ret_buf[VARIANT_BUF_SIZE] = {0};
    s_variant_new_nil((GDExtensionUninitializedVariantPtr)ret_buf);

    GDExtensionCallError error;
    s_object_call_script(classdb, (GDExtensionConstStringNamePtr)method_sn,
                          nullptr, 0, (GDExtensionUninitializedVariantPtr)ret_buf, &error);

    if (s_string_name_destructor) s_string_name_destructor((GDExtensionStringNamePtr)method_sn);

    // Result is a PackedStringArray — iterate it
    if (s_variant_get_type && s_variant_get_type((GDExtensionConstVariantPtr)ret_buf) == GDEXTENSION_VARIANT_TYPE_PACKED_STRING_ARRAY) {
        uint8_t iter_variant[VARIANT_BUF_SIZE] = {0};
        uint8_t elem_variant[VARIANT_BUF_SIZE] = {0};

        GDExtensionBool valid = false;
        bool has_more = s_variant_iter_init &&
            s_variant_iter_init((GDExtensionConstVariantPtr)ret_buf,
                                (GDExtensionUninitializedVariantPtr)iter_variant, &valid);

        while (has_more && valid) {
            if (s_variant_iter_get) {
                s_variant_new_nil((GDExtensionUninitializedVariantPtr)elem_variant);
                s_variant_iter_get((GDExtensionConstVariantPtr)ret_buf,
                                   (GDExtensionVariantPtr)iter_variant,
                                   (GDExtensionUninitializedVariantPtr)elem_variant, &valid);

                std::string name = variant_to_string((GDExtensionConstVariantPtr)elem_variant);
                if (!name.empty()) result.push_back(name);
                s_variant_destroy((GDExtensionVariantPtr)elem_variant);
            }
            if (s_variant_iter_next) {
                has_more = s_variant_iter_next((GDExtensionConstVariantPtr)ret_buf,
                                               (GDExtensionVariantPtr)iter_variant, &valid);
            } else {
                break;
            }
        }
        s_variant_destroy((GDExtensionVariantPtr)iter_variant);
    }

    s_variant_destroy((GDExtensionVariantPtr)ret_buf);
    return result;
}

std::string GodotAPI::classdb_get_parent_class(const std::string &p_class_name) {
    if (!s_global_get_singleton || !s_string_name_new_latin1 || !s_object_call_script || !s_variant_new_nil || !s_variant_destroy) {
        return "";
    }

    uint8_t classdb_sn[8];
    make_string_name(classdb_sn, "ClassDB");
    GDExtensionObjectPtr classdb = s_global_get_singleton((GDExtensionConstStringNamePtr)classdb_sn);
    if (s_string_name_destructor) s_string_name_destructor((GDExtensionStringNamePtr)classdb_sn);
    if (!classdb) return "";

    uint8_t class_name_variant[VARIANT_BUF_SIZE] = {0};
    make_variant_string((GDExtensionUninitializedVariantPtr)class_name_variant, p_class_name);

    GDExtensionConstVariantPtr args[] = { (GDExtensionConstVariantPtr)class_name_variant };

    uint8_t method_sn[8];
    make_string_name(method_sn, "get_parent_class");

    uint8_t ret_buf[VARIANT_BUF_SIZE] = {0};
    s_variant_new_nil((GDExtensionUninitializedVariantPtr)ret_buf);

    GDExtensionCallError error;
    s_object_call_script(classdb, (GDExtensionConstStringNamePtr)method_sn,
                          args, 1, (GDExtensionUninitializedVariantPtr)ret_buf, &error);

    if (s_string_name_destructor) s_string_name_destructor((GDExtensionStringNamePtr)method_sn);

    std::string parent = variant_to_string((GDExtensionConstVariantPtr)ret_buf);
    s_variant_destroy((GDExtensionVariantPtr)ret_buf);
    destroy_variant((GDExtensionVariantPtr)class_name_variant);

    return parent;
}

bool GodotAPI::classdb_is_parent_class(const std::string &p_class_name, const std::string &p_parent) {
    std::string current = p_class_name;
    while (!current.empty()) {
        if (current == p_parent) return true;
        current = classdb_get_parent_class(current);
    }
    return false;
}

// ===========================================================================
// Scene operations
// ===========================================================================

GDExtensionObjectPtr GodotAPI::load_scene(const std::string &p_res_path) {
    GDExtensionObjectPtr editor = get_editor_interface();
    if (!editor) return nullptr;

    // EditorInterface::open_scene_from_path(path)
    // Then get the edited scene root
    uint8_t path_variant[VARIANT_BUF_SIZE] = {0};
    make_variant_string((GDExtensionUninitializedVariantPtr)path_variant, p_res_path);

    GDExtensionConstVariantPtr args[] = { (GDExtensionConstVariantPtr)path_variant };
    call_method_void(editor, "open_scene_from_path", args, 1);

    destroy_variant((GDExtensionVariantPtr)path_variant);

    return get_edited_scene_root();
}

bool GodotAPI::save_scene(GDExtensionObjectPtr p_root, const std::string &p_res_path) {
    GDExtensionObjectPtr editor = get_editor_interface();
    if (!editor) return false;

    // Try EditorInterface::save_scene()
    // This saves the current scene
    call_method_void(editor, "save_scene", nullptr, 0);
    return true;
}

void GodotAPI::refresh_filesystem() {
    GDExtensionObjectPtr editor = get_editor_interface();
    if (!editor) return;

    // Get FileSystemDock and call scan()
    GDExtensionObjectPtr fs_dock = call_method_object(editor, "get_file_system_dock");
    if (fs_dock) {
        call_method_void(fs_dock, "scan", nullptr, 0);
    }
}

// ===========================================================================
// Resource operations
// ===========================================================================

GDExtensionObjectPtr GodotAPI::load_resource(const std::string &p_res_path) {
    // Get ResourceLoader singleton and call load()
    if (!s_global_get_singleton || !s_string_name_new_latin1) return nullptr;

    uint8_t loader_sn[8];
    make_string_name(loader_sn, "ResourceLoader");
    GDExtensionObjectPtr loader = s_global_get_singleton((GDExtensionConstStringNamePtr)loader_sn);
    if (s_string_name_destructor) s_string_name_destructor((GDExtensionStringNamePtr)loader_sn);

    if (!loader) return nullptr;

    uint8_t path_variant[VARIANT_BUF_SIZE] = {0};
    make_variant_string((GDExtensionUninitializedVariantPtr)path_variant, p_res_path);

    GDExtensionConstVariantPtr args[] = { (GDExtensionConstVariantPtr)path_variant };
    GDExtensionObjectPtr result = call_method_object_with_args(loader, "load", args, 1);

    destroy_variant((GDExtensionVariantPtr)path_variant);
    return result;
}

bool GodotAPI::save_resource(GDExtensionObjectPtr p_resource, const std::string &p_path) {
    if (!s_global_get_singleton || !s_string_name_new_latin1) return false;

    uint8_t saver_sn[8];
    make_string_name(saver_sn, "ResourceSaver");
    GDExtensionObjectPtr saver = s_global_get_singleton((GDExtensionConstStringNamePtr)saver_sn);
    if (s_string_name_destructor) s_string_name_destructor((GDExtensionStringNamePtr)saver_sn);

    if (!saver) return false;

    uint8_t res_variant[VARIANT_BUF_SIZE] = {0};
    uint8_t path_variant[VARIANT_BUF_SIZE] = {0};
    make_variant_object((GDExtensionUninitializedVariantPtr)res_variant, p_resource);
    make_variant_string((GDExtensionUninitializedVariantPtr)path_variant, p_path);

    GDExtensionConstVariantPtr args[] = {
        (GDExtensionConstVariantPtr)res_variant,
        (GDExtensionConstVariantPtr)path_variant
    };

    std::string result = call_method_with_args(saver, "save", args, 2);

    destroy_variant((GDExtensionVariantPtr)res_variant);
    destroy_variant((GDExtensionVariantPtr)path_variant);

    // save() returns Error enum (OK = 0)
    return result == "0" || result == "OK";
}

// ===========================================================================
// Signal operations
// ===========================================================================

bool GodotAPI::connect_signal(GDExtensionObjectPtr p_source, const std::string &p_signal,
                                GDExtensionObjectPtr p_target, const std::string &p_method, int p_flags) {
    if (!p_source || !p_target) return false;

    // source.connect(signal_name, Callable(target, method_name))
    // We need to create a Callable variant
    // For now, use call_method_void with appropriate args

    // connect(StringName signal, Callable callable, int flags = 0)
    // Creating a Callable is complex in GDExtension C API
    // Use the simpler approach: connect via script method
    uint8_t signal_variant[VARIANT_BUF_SIZE] = {0};
    uint8_t target_variant[VARIANT_BUF_SIZE] = {0};
    uint8_t method_variant[VARIANT_BUF_SIZE] = {0};
    uint8_t flags_variant[VARIANT_BUF_SIZE] = {0};

    make_variant_string((GDExtensionUninitializedVariantPtr)signal_variant, p_signal);
    make_variant_object((GDExtensionUninitializedVariantPtr)target_variant, p_target);
    make_variant_string((GDExtensionUninitializedVariantPtr)method_variant, p_method);
    make_variant_int((GDExtensionUninitializedVariantPtr)flags_variant, p_flags);

    // Use the two-step approach: connect(signal, Callable(target, method))
    // Since Callable construction is complex, we'll use a workaround:
    // Call the Node's connect method with string args and let Godot handle it
    // Actually, we need a proper Callable. Let's try using object_call_script_method
    // which can handle the auto-conversion

    // For Phase 2, we'll write the .tscn signal connections as text
    // and use the API for runtime connections

    // Attempt: pass signal, target, method as separate args (Godot 3 style)
    // This won't work in Godot 4 which uses Callable
    // We need to construct a Callable variant

    // Workaround: use the editor's signal connection dialog programmatically
    // Or just write the connection in the .tscn file

    destroy_variant((GDExtensionVariantPtr)signal_variant);
    destroy_variant((GDExtensionVariantPtr)target_variant);
    destroy_variant((GDExtensionVariantPtr)method_variant);
    destroy_variant((GDExtensionVariantPtr)flags_variant);

    // TODO: Implement proper Callable construction for Phase 2
    // For now, signal connections will be done via .tscn file editing
    std::fprintf(stderr, "[Gear MCP] connect_signal: Callable construction not yet implemented\n");
    return false;
}

void GodotAPI::disconnect_signal(GDExtensionObjectPtr p_source, const std::string &p_signal,
                                    GDExtensionObjectPtr p_target, const std::string &p_method) {
    if (!p_source || !p_target) return;
    // Same Callable construction issue as connect_signal
    std::fprintf(stderr, "[Gear MCP] disconnect_signal: Callable construction not yet implemented\n");
}

// ===========================================================================
// Process management
// ===========================================================================

int GodotAPI::spawn_process(const std::string &p_executable, const std::vector<std::string> &p_args) {
#ifdef _WIN32
    std::string cmd = "\"" + p_executable + "\"";
    for (const auto &arg : p_args) {
        cmd += " \"" + arg + "\"";
    }

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    if (!CreateProcessA(nullptr, (LPSTR)cmd.c_str(), nullptr, nullptr, FALSE,
                         CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return -1;
    }

    CloseHandle(pi.hThread);
    // Don't close hProcess - we need it for is_process_running
    // Store handle somewhere? For now, just return PID
    CloseHandle(pi.hProcess);
    return (int)pi.dwProcessId;
#else
    pid_t pid = fork();
    if (pid == 0) {
        // Child
        std::vector<const char *> argv;
        argv.push_back(p_executable.c_str());
        for (const auto &arg : p_args) {
            argv.push_back(arg.c_str());
        }
        argv.push_back(nullptr);
        execvp(p_executable.c_str(), (char *const *)argv.data());
        _exit(1);
    }
    return (int)pid;
#endif
}

bool GodotAPI::kill_process(int p_pid) {
    if (p_pid <= 0) return false;
#ifdef _WIN32
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)p_pid);
    if (!hProcess) return false;
    BOOL result = TerminateProcess(hProcess, 1);
    CloseHandle(hProcess);
    return result != 0;
#else
    return kill(p_pid, SIGTERM) == 0;
#endif
}

// ===========================================================================
// Context snapshot
// ===========================================================================

std::string GodotAPI::get_context_snapshot() {
    std::string scene_open;
    std::string project_path = get_project_path();
    std::string project_name = "Unknown";
    std::string godot_version = get_godot_version();
    std::string selected_nodes_json = "[]";

    // Try reading project.godot for project name
    {
        std::ifstream proj_file(project_path + "/project.godot");
        if (proj_file.is_open()) {
            std::string line;
            while (std::getline(proj_file, line)) {
                if (line.size() > 5 && line[0] != ';' && line[0] != '#') {
                    size_t eq_pos = line.find('=');
                    if (eq_pos != std::string::npos) {
                        std::string key = line.substr(0, eq_pos);
                        size_t s = key.find_first_not_of(" \t\r\n");
                        size_t e = key.find_last_not_of(" \t\r\n");
                        if (s != std::string::npos) key = key.substr(s, e - s + 1);

                        if (key == "config/name" || key == "application/name") {
                            std::string value = line.substr(eq_pos + 1);
                            s = value.find_first_not_of(" \t\r\n\"");
                            e = value.find_last_not_of(" \t\r\n\"");
                            if (s != std::string::npos) project_name = value.substr(s, e - s + 1);
                            break;
                        }
                    }
                }
            }
        }
    }

    // Try EditorInterface
    GDExtensionObjectPtr editor = get_editor_interface();
    if (editor) {
        GDExtensionObjectPtr scene_root = get_edited_scene_root();
        if (scene_root) {
            scene_open = call_method_string(scene_root, "get_scene_file_path");
        }

        std::vector<GDExtensionObjectPtr> selected = get_selected_nodes();
        selected_nodes_json = "[";
        for (size_t i = 0; i < selected.size(); i++) {
            std::string name = get_node_name(selected[i]);
            if (!name.empty()) {
                if (i > 0) selected_nodes_json += ",";
                selected_nodes_json += "\"" + escape_json(name) + "\"";
            }
        }
        selected_nodes_json += "]";
    }

    std::string json = "{";
    json += "\"scene_open\":\"" + escape_json(scene_open) + "\",";
    json += "\"selected_nodes\":" + selected_nodes_json + ",";
    json += "\"project_name\":\"" + escape_json(project_name) + "\",";
    json += "\"project_path\":\"" + escape_json(project_path) + "\",";
    json += "\"godot_version\":\"" + escape_json(godot_version) + "\"";
    json += "}";

    return json;
}

} // namespace gear_mcp
