#if defined(CONFIG_IPIPE) && !defined(IPIPE_ARCH_STRING)
#define IPIPE_ARCH_STRING	"x86_32/64 merge"
#define IPIPE_MAJOR_NUMBER	0
#define IPIPE_MINOR_NUMBER	0
#define IPIPE_PATCH_NUMBER	0
#endif
#ifdef CONFIG_X86_32
# include "ipipe_32.h"
#else
# include "ipipe_64.h"
#endif
