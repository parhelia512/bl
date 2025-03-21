#ifndef BL_ASSEMBLY_BL
#define BL_ASSEMBLY_BL

#include "arena.h"
#include "atomics.h"
#include "common.h"
#include "mir.h"
#include "scope.h"
#include "threading.h"
#include "tinycthread.h"
#include "unit.h"

#include <dyncall.h>
#include <dynload.h>

#define TRIPLE_MAX_LEN 128

struct builder;
struct builder_options;

enum scope_dump_mode {
	SCOPE_DUMP_MODE_PARENTING,
	SCOPE_DUMP_MODE_INJECTION,
};

enum assembly_kind {
	ASSEMBLY_EXECUTABLE     = 0,
	ASSEMBLY_SHARED_LIB     = 1,
	ASSEMBLY_BUILD_PIPELINE = 2,
	ASSEMBLY_DOCS           = 3,
};

enum assembly_opt {
	ASSEMBLY_OPT_DEBUG                   = 0, // Standard debug mode. Opt: NONE
	ASSEMBLY_OPT_RELEASE_FAST            = 1, // Standard release mode. Opt: Aggressive
	ASSEMBLY_OPT_RELEASE_SMALL           = 2, // Standard release mode. Opt: Default
	ASSEMBLY_OPT_RELEASE_WITH_DEBUG_INFO = 3, // Release mode. Opt: Default with debug info.
};

enum assembly_di_kind {
	ASSEMBLY_DI_DWARF    = 0, // Emit DWARF debug information in LLVM IR.
	ASSEMBLY_DI_CODEVIEW = 1, // Emit MS CodeView debug info (PDB file).
};

enum assert_mode {
	ASSERT_DEFAULT         = 0,
	ASSERT_ALWAYS_ENABLED  = 1,
	ASSERT_ALWAYS_DISABLED = 2,
};

enum arch {
#define GEN_ARCH
#define entry(X) ARCH_##X,
#include "target.def"
#undef entry
#undef GEN_ARCH
	_ARCH_COUNT
};
extern const char *arch_names[_ARCH_COUNT];

enum vendor {
#define GEN_VENDOR
#define entry(X) VENDOR_##X,
#include "target.def"
#undef entry
#undef GEN_VENDOR
	_VENDOR_COUNT
};
extern const char *vendor_names[_VENDOR_COUNT];

enum operating_system {
#define GEN_OS
#define entry(X) OS_##X,
#include "target.def"
#undef entry
#undef GEN_OS
	_OS_COUNT
};
extern const char *os_names[_OS_COUNT];

enum environment {
#define GEN_ENV
#define entry(X) ENV_##X,
#include "target.def"
#undef entry
#undef GEN_ENV
	_ENV_COUNT
};
extern const char *env_names[_ENV_COUNT];

struct target_triple {
	enum arch             arch;
	enum vendor           vendor;
	enum operating_system os;
	enum environment      env;
};

enum native_lib_flags {
	// Library is used only internally during compilation and VM execution.
	NATIVE_LIB_FLAG_RUNTIME = 1 << 0,
	// Library is used only in runtime (passed to native platform linker).
	NATIVE_LIB_FLAG_COMPTIME = 1 << 1,
	// Library is not comming from modules.
	NATIVE_LIB_IS_SYSTEM = 1 << 2,
};

struct native_lib {
	hash_t        hash;
	DLLib        *handle;
	struct token *linked_from;
	str_t         user_name;
	str_t         filename;
	str_t         filepath;
	str_t         dir;

	enum native_lib_flags flags;
};

struct module {
	struct scope *scope;
	struct scope *private_scope; // Introduced by #scope_module block
	str_t         modulepath;
	hash_t        hash;
};

// ABI sync!!! Keep this updated with target representation in build.bl.
#define TARGET_COPYABLE_CONTENT                        \
	enum assembly_kind    kind;                        \
	enum assembly_opt     opt;                         \
	enum assembly_di_kind di;                          \
	bool                  reg_split;                   \
	bool                  verify_llvm;                 \
	bool                  run_tests;                   \
	bool                  tests_minimal_output;        \
	bool                  no_api;                      \
	bool                  copy_deps;                   \
	bool                  run;                         \
	bool                  print_tokens;                \
	bool                  print_ast;                   \
	bool                  print_scopes;                \
	enum scope_dump_mode  print_scopes_mode;           \
	bool                  emit_llvm;                   \
	bool                  emit_mir;                    \
	bool                  emit_asm;                    \
	bool                  no_bin;                      \
	bool                  no_llvm;                     \
	bool                  no_analyze;                  \
	bool                  x64;                         \
	enum assert_mode      assert_mode;                 \
	bool                  syntax_only;                 \
	bool                  vmdbg_enabled;               \
	s32                   vmdbg_break_on;              \
	bool                  enable_experimental_targets; \
	struct target_triple  triple;

struct target {
	// Copyable content of target can be duplicated from default target, the default target is
	// usually target containing some setup acquired from command line arguments of application.
	TARGET_COPYABLE_CONTENT

	char *name;
	array(char *) files;
	array(char *) default_lib_paths;
	array(char *) default_libs;
	str_buf_t default_custom_linker_opt;
	str_buf_t out_dir;
	str_buf_t module_dir;

	struct {
		s32    argc;
		char **argv;
	} vm;

	bmagic_member
};

struct assembly_thread_local_context {
	struct scope_thread_local scope_thread_local;
	struct mir_arenas         mir_arenas;
	struct arena              small_array;
	struct arena              ast_arena;
	struct string_cache      *string_cache;
};

struct assembly {
	const struct target *target;
	str_buf_t            custom_linker_opt;
	spl_t                custom_linker_opt_lock;

	array(char *) lib_paths;
	spl_t lib_paths_lock;

	array(struct native_lib) libs;
	spl_t libs_lock;

	// We have group of thread local contexts for each worker thread to prevent locking.
	array(struct assembly_thread_local_context) thread_local_contexts;

	struct mir mir;

	struct {
		LLVMModuleRef        module;
		llvm_context_ref_t   ctx;
		LLVMTargetDataRef    TD;
		LLVMTargetMachineRef TM;
		char                *triple;
	} llvm;

	struct {
		array(struct mir_fn *) cases;
		struct mir_var *meta_var;

		// Expected unit test count is evaluated before analyze pass. We need this
		// information before we analyze all test functions because metadata runtime
		// variable must be preallocated (testcases builtin operator cannot wait for all
		// test case functions to be analyzed). This count must match cases len in the end.
		s32 expected_test_count;
	} testing;

	struct {
		struct mir_fn  *entry;                  // Main function
		struct mir_fn  *build_entry;            // Set for build assembly
		struct mir_var *command_line_arguments; // Command line arguments variable.
		// Provide information whether application run in compile time or not.
		struct mir_var *is_comptime_run;

		// Store status of last execution of this assembly.
		s32 last_execution_status;
	} vm_run;

	// Some compilation time related runtimes, this data are reset for every compilation.
	struct {
		batomic_s32 lexing_ms;
		batomic_s32 parsing_ms;
		batomic_s32 mir_generate_ms;
		batomic_s32 mir_analyze_ms;
		batomic_s32 llvm_ms;
		batomic_s32 llvm_obj_ms;
		batomic_s32 linking_ms;
		batomic_s32 polymorph_ms;

		batomic_s32 polymorph_count; // @Incomplete: rename to generated.
		batomic_s32 comptime_call_stacks_count;
	} stats;

	// DynCall/Lib data used for external method execution in compile time
	DCCallVM              *dc_vm;
	struct virtual_machine vm;

	array(struct unit *) units; // array of all units in assembly
	mtx_t units_lock;

	array(struct module *) modules;
	mtx_t modules_lock;

	struct scope *gscope; // global scope of the assembly
	struct scope *module_scope;

	/* Builtins */
	struct builtin_types {
#define GEN_BUILTIN_TYPES
#include "assembly.def"
#undef GEN_BUILTIN_TYPES
		bool is_rtti_ready;
		bool is_any_ready;
		bool is_test_cases_ready;
		bool is_error_ready;
	} builtin_types;

	// These are just cached from the builder and valid only in case the assembly is
	// compiling.
	struct {
		unit_stage_fn_t     *unit;
		assembly_stage_fn_t *assembly;
	} current_pipelines;
};

struct target *target_new(const char *name);
struct target *target_dup(const char *name, const struct target *other);
void           target_delete(struct target *target);
void           target_add_file(struct target *target, const char *filepath);
void           target_add_lib_path(struct target *target, const char *path);
void           target_add_lib(struct target *target, const char *lib);
void           target_append_linker_options(struct target *target, const char *option);
void           target_set_vm_args(struct target *target, s32 argc, char **argv);
void           target_set_output_dir(struct target *target, const char *dirpath);
void           target_set_module_dir(struct target *target,
                                     const char    *dir);
bool           target_is_triple_valid(struct target_triple *triple);
bool           target_init_default_triple(struct target_triple *triple);
s32            target_triple_to_string(const struct target_triple *triple, char *buf, s32 buf_len);

struct assembly *assembly_new(const struct target *target);
void             assembly_delete(struct assembly *assembly);
void             assembly_add_unit(struct assembly *assembly, const str_t filepath, struct token *load_from, struct scope *parent_scope, struct module *module);
void             assembly_add_lib_path(struct assembly *assembly, const char *path);
void             assembly_append_linker_options(struct assembly *assembly, const char *opt);
void             assembly_add_native_lib(struct assembly      *assembly,
                                         const char           *lib_name,
                                         struct token         *link_token,
                                         enum native_lib_flags flags);
struct module   *assembly_import_module(struct assembly *assembly,
                                        str_t            modulepath,
                                        struct token    *import_from,
                                        struct scope    *scope);
DCpointer        assembly_find_extern(struct assembly *assembly, const str_t symbol);

// Print the top-level scope structure as dot graph.
void assembly_dump_scope_structure(struct assembly *assembly, FILE *stream, enum scope_dump_mode mode);

// Convert opt level to string.
static inline const char *opt_to_str(enum assembly_opt opt) {
	switch (opt) {
	case ASSEMBLY_OPT_DEBUG:
		return "DEBUG";
	case ASSEMBLY_OPT_RELEASE_FAST:
		return "RELEASE-FAST";
	case ASSEMBLY_OPT_RELEASE_SMALL:
		return "RELEASE-SMALL";
	case ASSEMBLY_OPT_RELEASE_WITH_DEBUG_INFO:
		return "RELEASE-WITH-DEBUG-INFO";
	}
	babort("Invalid build mode");
}

// Convert opt level to LLVM.
static inline LLVMCodeGenOptLevel opt_to_LLVM(enum assembly_opt opt) {
	switch (opt) {
	case ASSEMBLY_OPT_DEBUG:
		return LLVMCodeGenLevelNone;
	case ASSEMBLY_OPT_RELEASE_FAST:
		return LLVMCodeGenLevelAggressive;
	case ASSEMBLY_OPT_RELEASE_SMALL:
	case ASSEMBLY_OPT_RELEASE_WITH_DEBUG_INFO:
		return LLVMCodeGenLevelDefault;
	}
	babort("Invalid build mode");
}

static inline str_t opt_to_LLVM_pass_str(enum assembly_opt opt) {
	switch (opt) {
	case ASSEMBLY_OPT_DEBUG:
		return cstr("O0");
	case ASSEMBLY_OPT_RELEASE_FAST:
		return cstr("O3");
	case ASSEMBLY_OPT_RELEASE_SMALL:
		return cstr("Os");
	case ASSEMBLY_OPT_RELEASE_WITH_DEBUG_INFO:
		return cstr("O2");
	}
	babort("Invalid build mode");
}

#endif
