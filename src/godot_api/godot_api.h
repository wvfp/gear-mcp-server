#pragma once

#include <gdextension_interface.h>
#include <string>
#include <vector>

namespace gear_mcp {

// ---------------------------------------------------------------------------
// Custom function pointer typedefs for GDExtension functions
// that don't have named typedefs in the generated header.
// ---------------------------------------------------------------------------

typedef GDExtensionObjectPtr (*GearFuncClassdbConstructObject)(GDExtensionConstStringNamePtr p_classname);
typedef GDExtensionMethodBindPtr (*GearFuncClassdbGetMethodBind)(GDExtensionConstStringNamePtr p_classname, GDExtensionConstStringNamePtr p_methodname, GDExtensionInt p_hash);
typedef void (*GearFuncObjectMethodBindCall)(GDExtensionMethodBindPtr p_method_bind, GDExtensionObjectPtr p_instance, const GDExtensionConstVariantPtr *p_args, GDExtensionInt p_arg_count, GDExtensionUninitializedVariantPtr r_ret, GDExtensionCallError *r_error);
typedef void (*GearFuncObjectDestroy)(GDExtensionObjectPtr p_object);
typedef void (*GearFuncVariantBooleanize)(GDExtensionConstVariantPtr p_variant, GDExtensionBool *r_ret);
typedef void (*GearFuncStringNewWithUtf8Chars)(GDExtensionUninitializedStringPtr r_string, const char *p_contents, GDExtensionInt p_size);
typedef void (*GearFuncStringNewWithLatin1Chars)(GDExtensionUninitializedStringPtr r_string, const char *p_contents, GDExtensionBool p_is_static);

// ---------------------------------------------------------------------------
// GodotAPI — singleton that resolves GDExtension C API function pointers
// at init time and provides high-level editor access methods.
// ---------------------------------------------------------------------------
class GodotAPI {
public:
    static void init(GDExtensionInterfaceGetProcAddress p_get_proc_address);

    // ---- EditorInterface access ----
    static GDExtensionObjectPtr get_editor_interface();
    static GDExtensionObjectPtr get_edited_scene_root();
    static GDExtensionObjectPtr get_selection();
    static std::vector<GDExtensionObjectPtr> get_selected_nodes();
    static GDExtensionObjectPtr get_editor_undo_redo();
    static std::string get_project_path();
    static std::string res_to_absolute(const std::string &p_res_path);
    static std::string absolute_to_res(const std::string &p_abs_path);

    // ---- Context snapshot (JSON) ----
    static std::string get_context_snapshot();

    // ---- Scene operations ----
    static GDExtensionObjectPtr load_scene(const std::string &p_res_path);
    static bool save_scene(GDExtensionObjectPtr p_root, const std::string &p_res_path);
    static void refresh_filesystem();

    // ---- Node operations ----
    static GDExtensionObjectPtr create_node(const std::string &p_class_name);
    static void add_child(GDExtensionObjectPtr p_parent, GDExtensionObjectPtr p_child);
    static void remove_child(GDExtensionObjectPtr p_parent, GDExtensionObjectPtr p_child);
    static void set_owner(GDExtensionObjectPtr p_node, GDExtensionObjectPtr p_owner);
    static void set_owner_recursive(GDExtensionObjectPtr p_node, GDExtensionObjectPtr p_owner);
    static std::string get_node_name(GDExtensionObjectPtr p_node);
    static std::string get_node_class(GDExtensionObjectPtr p_node);
    static GDExtensionObjectPtr get_node_child(GDExtensionObjectPtr p_node, const std::string &p_path);
    static int get_child_count(GDExtensionObjectPtr p_node);
    static std::vector<GDExtensionObjectPtr> get_children(GDExtensionObjectPtr p_node);
    static GDExtensionObjectPtr duplicate_node(GDExtensionObjectPtr p_node, int p_flags = 0);
    static void queue_free(GDExtensionObjectPtr p_node);

    // ---- Property operations ----
    static bool set_property(GDExtensionObjectPtr p_obj, const std::string &p_name,
                              GDExtensionConstVariantPtr p_value);
    static bool get_property(GDExtensionObjectPtr p_obj, const std::string &p_name,
                              GDExtensionUninitializedVariantPtr r_value);
    static std::string get_property_json(GDExtensionObjectPtr p_obj, const std::string &p_name);
    static GDExtensionObjectPtr get_property_object(GDExtensionObjectPtr p_obj, const std::string &p_name);
    static bool set_property_from_json(GDExtensionObjectPtr p_obj, const std::string &p_name,
                                        const std::string &p_json);

    // ---- Variant construction helpers ----
    static void make_variant_nil(GDExtensionUninitializedVariantPtr r_variant);
    static void make_variant_bool(GDExtensionUninitializedVariantPtr r_variant, bool p_value);
    static void make_variant_int(GDExtensionUninitializedVariantPtr r_variant, int64_t p_value);
    static void make_variant_float(GDExtensionUninitializedVariantPtr r_variant, double p_value);
    static void make_variant_string(GDExtensionUninitializedVariantPtr r_variant, const std::string &p_value);
    static void make_variant_object(GDExtensionUninitializedVariantPtr r_variant, GDExtensionObjectPtr p_obj);
    static void destroy_variant(GDExtensionVariantPtr p_variant);
    static GDExtensionVariantType get_variant_type(GDExtensionConstVariantPtr p_variant);
    static int64_t variant_to_int(GDExtensionConstVariantPtr p_variant);
    static double variant_to_float(GDExtensionConstVariantPtr p_variant);
    static bool variant_to_bool(GDExtensionConstVariantPtr p_variant);

    // ---- UndoRedo operations ----
    static void undo_redo_create_action(const std::string &p_name);
    static void undo_redo_add_do_method(GDExtensionObjectPtr p_obj, const std::string &p_method,
                                          const GDExtensionConstVariantPtr *p_args, int p_arg_count);
    static void undo_redo_add_undo_method(GDExtensionObjectPtr p_obj, const std::string &p_method,
                                            const GDExtensionConstVariantPtr *p_args, int p_arg_count);
    static void undo_redo_add_do_property(GDExtensionObjectPtr p_obj, const std::string &p_name,
                                            GDExtensionConstVariantPtr p_value);
    static void undo_redo_add_undo_property(GDExtensionObjectPtr p_obj, const std::string &p_name,
                                              GDExtensionConstVariantPtr p_value);
    static void undo_redo_add_do_reference(GDExtensionObjectPtr p_obj);
    static void undo_redo_add_undo_reference(GDExtensionObjectPtr p_obj);
    static void undo_redo_commit_action();

    // ---- ClassDB queries ----
    static bool classdb_class_exists(const std::string &p_class_name);
    static std::vector<std::string> classdb_get_class_list();
    static std::string classdb_get_parent_class(const std::string &p_class_name);
    static bool classdb_is_parent_class(const std::string &p_class_name, const std::string &p_parent);

    // ---- Resource operations ----
    static GDExtensionObjectPtr load_resource(const std::string &p_res_path);
    static bool save_resource(GDExtensionObjectPtr p_resource, const std::string &p_path);

    // ---- Signal operations ----
    static bool connect_signal(GDExtensionObjectPtr p_source, const std::string &p_signal,
                                GDExtensionObjectPtr p_target, const std::string &p_method, int p_flags = 0);
    static void disconnect_signal(GDExtensionObjectPtr p_source, const std::string &p_signal,
                                    GDExtensionObjectPtr p_target, const std::string &p_method);

    // ---- Process management ----
    static int spawn_process(const std::string &p_executable, const std::vector<std::string> &p_args);
    static bool kill_process(int p_pid);

    // ---- GDExtension method call helpers ----
    static std::string call_method_string(GDExtensionObjectPtr p_obj, const char *p_method);
    static GDExtensionObjectPtr call_method_object(GDExtensionObjectPtr p_obj, const char *p_method);
    static int call_method_int(GDExtensionObjectPtr p_obj, const char *p_method);
    static double call_method_float(GDExtensionObjectPtr p_obj, const char *p_method);
    static bool call_method_bool(GDExtensionObjectPtr p_obj, const char *p_method);
    static void call_method_void(GDExtensionObjectPtr p_obj, const char *p_method,
                                  const GDExtensionConstVariantPtr *p_args, int p_arg_count);
    static std::string call_method_with_args(GDExtensionObjectPtr p_obj, const char *p_method,
                                              const GDExtensionConstVariantPtr *p_args, int p_arg_count);
    static int call_method_int_with_args(GDExtensionObjectPtr p_obj, const char *p_method,
                                          const GDExtensionConstVariantPtr *p_args, int p_arg_count);
    static double call_method_float_with_args(GDExtensionObjectPtr p_obj, const char *p_method,
                                                const GDExtensionConstVariantPtr *p_args, int p_arg_count);
    static bool call_method_bool_with_args(GDExtensionObjectPtr p_obj, const char *p_method,
                                            const GDExtensionConstVariantPtr *p_args, int p_arg_count);
    static GDExtensionObjectPtr call_method_object_with_args(GDExtensionObjectPtr p_obj, const char *p_method,
                                                               const GDExtensionConstVariantPtr *p_args, int p_arg_count);

    // ---- Global singleton helpers ----
    static GDExtensionObjectPtr get_global_singleton(const char *p_name);

    // ---- Variant helpers ----
    static std::string variant_to_string(GDExtensionConstVariantPtr p_variant);
    static GDExtensionObjectPtr variant_to_object(GDExtensionConstVariantPtr p_variant);

    // ---- String helpers ----
    static std::string escape_json(const std::string &p_input);

    // ---- Engine info ----
    static std::string get_godot_version();
    static std::string get_executable_path();

    static bool is_initialized() { return s_initialized; }

private:
    static void make_string_name(uint8_t p_buf[8], const char *p_name);

    // ---- Phase 1 function pointers ----
    static GDExtensionInterfaceGlobalGetSingleton s_global_get_singleton;
    static GDExtensionInterfaceObjectCallScriptMethod s_object_call_script;
    static GDExtensionInterfaceVariantNewNil s_variant_new_nil;
    static GDExtensionInterfaceVariantDestroy s_variant_destroy;
    static GDExtensionInterfaceVariantNewCopy s_variant_new_copy;
    static GDExtensionInterfaceVariantGetType s_variant_get_type;
    static GDExtensionInterfaceVariantStringify s_variant_stringify;
    static GDExtensionInterfaceVariantGetObjectInstanceId s_variant_get_object_instance_id;
    static GDExtensionInterfaceObjectGetInstanceFromId s_object_get_instance_from_id;
    static GDExtensionInterfaceStringNameNewWithLatin1Chars s_string_name_new_latin1;
    static GDExtensionInterfaceStringToUtf8Chars s_string_to_utf8_chars;
    static GDExtensionInterfaceVariantGetPtrDestructor s_variant_get_ptr_destructor;
    static GDExtensionInterfaceVariantIterInit s_variant_iter_init;
    static GDExtensionInterfaceVariantIterNext s_variant_iter_next;
    static GDExtensionInterfaceVariantIterGet s_variant_iter_get;
    static GDExtensionInterfaceGetNativeStructSize s_get_native_struct_size;

    // ---- Phase 2 function pointers ----
    static GearFuncClassdbConstructObject s_classdb_construct_object;
    static GearFuncClassdbGetMethodBind s_classdb_get_method_bind;
    static GearFuncObjectMethodBindCall s_object_method_bind_call;
    static GearFuncObjectDestroy s_object_destroy;
    static GearFuncVariantBooleanize s_variant_booleanize;
    static GearFuncStringNewWithUtf8Chars s_string_new_utf8;
    static GearFuncStringNewWithLatin1Chars s_string_new_latin1;

    // ---- Phase 2: variant type constructors ----
    static GDExtensionInterfaceGetVariantFromTypeConstructor s_variant_from_type;
    static GDExtensionInterfaceGetVariantToTypeConstructor s_variant_to_type;
    // Cached per-type constructors
    static GDExtensionVariantFromTypeConstructorFunc s_construct_bool;
    static GDExtensionVariantFromTypeConstructorFunc s_construct_int;
    static GDExtensionVariantFromTypeConstructorFunc s_construct_float;
    static GDExtensionVariantFromTypeConstructorFunc s_construct_string;
    static GDExtensionVariantFromTypeConstructorFunc s_construct_object;
    // Cached per-type extractors
    static GDExtensionTypeFromVariantConstructorFunc s_extract_int;
    static GDExtensionTypeFromVariantConstructorFunc s_extract_float;
    // Named property access via variant_set_named / variant_get_named
    static GDExtensionInterfaceVariantSetNamed s_variant_set_named;
    static GDExtensionInterfaceVariantGetNamed s_variant_get_named;

    static GDExtensionPtrDestructor s_string_destructor;
    static GDExtensionPtrDestructor s_string_name_destructor;

    static bool s_initialized;
};

static constexpr size_t VARIANT_BUF_SIZE = 64;

} // namespace gear_mcp
