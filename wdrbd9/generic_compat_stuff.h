#ifndef WDRBD9_GENERIC_COMPAT_STUFF
#define WDRBD9_GENERIC_COMPAT_STUFF



#define MODULE_AUTHOR(egal, ...)
#define MODULE_DESCRIPTION(egal, ...)
#define MODULE_VERSION(egal)
#define MODULE_LICENSE(egal)
#define MODULE_PARM_DESC(egal, ...)

#define module_param(...)

#define WARN(condition, format...) do {if(!!(condition)) printk(format)} while(0)
/* As good as it gets for now, don't know how to implement a true windows *ONCE* */
#define WARN_ONCE(condition, format...) WARN(condition, format)

#if 0
struct kmem_cache {
	NPAGED_LOOKASIDE_LIST cache;
};
#endif


#define BIO_ENDIO_ARGS(void1, void2) (void *p1, void *p2, void *p3)
#define MAKE_REQUEST_TYPE void

#define __always_inline inline
#define __inline inline

typedef char bool;
typedef int cpumask_var_t;

/* Yes, that'll be active for all structures...
 * But unless defined otherwise the compiler is free to choose alignment anyway. */
#define __packed


/* For shared/inaddr.h, struct in_addr */
#define FAR


#define BUILD_BUG_ON(expr)

#endif
