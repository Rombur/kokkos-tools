#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <dlfcn.h>
#include "../../profiling/all/kp_core.hpp"
#include "kp_config.hpp"

namespace KokkosTools {
namespace Sampler {
static uint64_t uniqID           = 0;
static uint64_t kernelSampleSkip = 101;
static int tool_verbosity        = 0;
static int tool_globFence        = 0;

// a hash table mapping kID to nestedkID
static std::unordered_map<uint64_t, uint64_t> infokIDSample;

typedef void (*initFunction)(const int, const uint64_t, const uint32_t, void*);
typedef void (*finalizeFunction)();
typedef void (*beginFunction)(const char*, const uint32_t, uint64_t*);
typedef void (*endFunction)(uint64_t);

static initFunction initProfileLibrary         = NULL;
static finalizeFunction finalizeProfileLibrary = NULL;
static beginFunction beginForCallee            = NULL;
static beginFunction beginScanCallee           = NULL;
static beginFunction beginReduceCallee         = NULL;
static endFunction endForCallee                = NULL;
static endFunction endScanCallee               = NULL;
static endFunction endReduceCallee             = NULL;

void kokkosp_request_tool_settings(const uint32_t,
                                   Kokkos_Tools_ToolSettings* settings) {
  if (0 == tool_globFence) {
    settings->requires_global_fencing = false;
  } else {
    settings->requires_global_fencing = true;
  }
}

void kokkosp_init_library(const int loadSeq, const uint64_t interfaceVer,
                          const uint32_t devInfoCount, void* deviceInfo) {
  const char* tool_verbose_str   = getenv("KOKKOS_TOOLS_SAMPLER_VERBOSE");
  const char* tool_globFence_str = getenv("KOKKOS_TOOLS_GLOBALFENCES");
  if (NULL != tool_verbose_str) {
    tool_verbosity = atoi(tool_verbose_str);
  } else {
    tool_verbosity = 0;
  }
  if (NULL != tool_globFence_str) {
    tool_globFence = atoi(tool_globFence_str);
  } else {
    tool_globFence = 0;
  }

  char* profileLibrary = getenv("KOKKOS_TOOLS_LIBS");
  if (NULL == profileLibrary) {
    printf(
        "Checking KOKKOS_PROFILE_LIBRARY. WARNING: This is a depreciated "
        "variable. Please use KOKKOS_TOOLS_LIBS\n");
    profileLibrary = getenv("KOKKOS_PROFILE_LIBRARY");
    if (NULL == profileLibrary) {
      printf("KokkosP: No library to call in %s\n", profileLibrary);
      exit(-1);
    }
  }

  char* envBuffer = (char*)malloc(sizeof(char) * (strlen(profileLibrary) + 1));
  strcpy(envBuffer, profileLibrary);

  char* nextLibrary = strtok(envBuffer, ";");

  for (int i = 0; i < loadSeq; i++) {
    nextLibrary = strtok(NULL, ";");
  }

  nextLibrary = strtok(NULL, ";");

  if (NULL == nextLibrary) {
    printf("KokkosP: No child library to call in %s\n", profileLibrary);
    exit(-1);
  } else {
    if (tool_verbosity > 0) {
      printf("KokkosP: Next library to call: %s\n", nextLibrary);
      printf("KokkosP: Loading child library ..\n");
    }

    void* childLibrary = dlopen(nextLibrary, RTLD_NOW | RTLD_GLOBAL);

    if (NULL == childLibrary) {
      fprintf(stderr, "KokkosP: Error: Unable to load: %s (Error=%s)\n",
              nextLibrary, dlerror());
      exit(-1);
    } else {
      beginForCallee =
          (beginFunction)dlsym(childLibrary, "kokkosp_begin_parallel_for");
      beginScanCallee =
          (beginFunction)dlsym(childLibrary, "kokkosp_begin_parallel_scan");
      beginReduceCallee =
          (beginFunction)dlsym(childLibrary, "kokkosp_begin_parallel_reduce");

      endScanCallee =
          (endFunction)dlsym(childLibrary, "kokkosp_end_parallel_scan");
      endForCallee =
          (endFunction)dlsym(childLibrary, "kokkosp_end_parallel_for");
      endReduceCallee =
          (endFunction)dlsym(childLibrary, "kokkosp_end_parallel_reduce");

      initProfileLibrary =
          (initFunction)dlsym(childLibrary, "kokkosp_init_library");
      finalizeProfileLibrary =
          (finalizeFunction)dlsym(childLibrary, "kokkosp_finalize_library");

      if (NULL != initProfileLibrary) {
        (*initProfileLibrary)(loadSeq + 1, interfaceVer, devInfoCount,
                              deviceInfo);
      }

      if (tool_verbosity > 0) {
        printf("KokkosP: Function Status:\n");
        printf("KokkosP: begin-parallel-for:      %s\n",
               (beginForCallee == NULL) ? "no" : "yes");
        printf("KokkosP: begin-parallel-scan:     %s\n",
               (beginScanCallee == NULL) ? "no" : "yes");
        printf("KokkosP: begin-parallel-reduce:   %s\n",
               (beginReduceCallee == NULL) ? "no" : "yes");
        printf("KokkosP: end-parallel-for:        %s\n",
               (endForCallee == NULL) ? "no" : "yes");
        printf("KokkosP: end-parallel-scan:       %s\n",
               (endScanCallee == NULL) ? "no" : "yes");
        printf("KokkosP: end-parallel-reduce:     %s\n",
               (endReduceCallee == NULL) ? "no" : "yes");
      }
    }
  }

  free(envBuffer);

  uniqID = 1;

  const char* tool_sample = getenv("KOKKOS_TOOLS_SAMPLER_SKIP");
  if (NULL != tool_sample) {
    kernelSampleSkip = atoi(tool_sample) + 1;
  }

  if (tool_verbosity > 0) {
    printf("KokkosP: Sampling rate set to: %s\n", tool_sample);
  }
}

void kokkosp_finalize_library() {
  if (NULL != finalizeProfileLibrary) (*finalizeProfileLibrary)();
}

void kokkosp_begin_parallel_for(const char* name, const uint32_t devID,
                                uint64_t* kID) {
  *kID                          = uniqID++;
  static uint64_t invocationNum = 0;
  ++invocationNum;
  if ((invocationNum % kernelSampleSkip) == 0) {
    if (tool_verbosity > 0) {
      printf("KokkosP: sample %llu calling child-begin function...\n",
             (unsigned long long)(*kID));
    }
    if (NULL != beginForCallee) {
      uint64_t nestedkID = 0;
      (*beginForCallee)(name, devID, &nestedkID);
      infokIDSample.insert({*kID, nestedkID});
    }
  }
}

void kokkosp_end_parallel_for(const uint64_t kID) {
  if (NULL != endForCallee) {
    if (!(infokIDSample.find(kID) == infokIDSample.end())) {
      uint64_t retrievedNestedkID = infokIDSample[kID];
      if (tool_verbosity > 0) {
        printf("KokkosP: sample %llu calling child-end function...\n",
               (unsigned long long)(kID));
      }
      (*endForCallee)(retrievedNestedkID);
    }
  }
}

void kokkosp_begin_parallel_scan(const char* name, const uint32_t devID,
                                 uint64_t* kID) {
  *kID                          = uniqID++;
  static uint64_t invocationNum = 0;
  ++invocationNum;
  if ((invocationNum % kernelSampleSkip) == 0) {
    if (tool_verbosity > 0) {
      printf("KokkosP: sample %llu calling child-begin function...\n",
             (unsigned long long)(*kID));
    }
    if (NULL != beginScanCallee) {
      uint64_t nestedkID = 0;
      (*beginScanCallee)(name, devID, &nestedkID);
      infokIDSample.insert({*kID, nestedkID});
    }
  }
}

void kokkosp_end_parallel_scan(const uint64_t kID) {
  if (NULL != endScanCallee) {
    if (!(infokIDSample.find(kID) == infokIDSample.end())) {
      uint64_t retrievedNestedkID = infokIDSample[kID];
      if (tool_verbosity > 0) {
        printf("KokkosP: sample %llu calling child-end function...\n",
               (unsigned long long)(kID));
      }
      (*endScanCallee)(retrievedNestedkID);
    }
  }
}

void kokkosp_begin_parallel_reduce(const char* name, const uint32_t devID,
                                   uint64_t* kID) {
  *kID                          = uniqID++;
  static uint64_t invocationNum = 0;
  ++invocationNum;
  if ((invocationNum % kernelSampleSkip) == 0) {
    if (tool_verbosity > 0) {
      printf("KokkosP: sample %llu calling child-begin function...\n",
             (unsigned long long)(*kID));
    }

    if (NULL != beginReduceCallee) {
      uint64_t nestedkID = 0;
      (*beginReduceCallee)(name, devID, &nestedkID);
      infokIDSample.insert({*kID, nestedkID});
    }
  }
}

void kokkosp_end_parallel_reduce(const uint64_t kID) {
  if (NULL != endScanCallee) {
    if (!(infokIDSample.find(kID) == infokIDSample.end())) {
      uint64_t retrievedNestedkID = infokIDSample[kID];
      if (tool_verbosity > 0) {
        printf("KokkosP: sample %llu calling child-end function...\n",
               (unsigned long long)(kID));
      }
      (*endScanCallee)(retrievedNestedkID);
    }
  }
}

}  // namespace Sampler
}  // end namespace KokkosTools

extern "C" {

namespace impl = KokkosTools::Sampler;

EXPOSE_TOOL_SETTINGS(impl::kokkosp_request_tool_settings)
EXPOSE_INIT(impl::kokkosp_init_library)
EXPOSE_FINALIZE(impl::kokkosp_finalize_library)
EXPOSE_BEGIN_PARALLEL_FOR(impl::kokkosp_begin_parallel_for)
EXPOSE_END_PARALLEL_FOR(impl::kokkosp_end_parallel_for)
EXPOSE_BEGIN_PARALLEL_SCAN(impl::kokkosp_begin_parallel_scan)
EXPOSE_END_PARALLEL_SCAN(impl::kokkosp_end_parallel_scan)
EXPOSE_BEGIN_PARALLEL_REDUCE(impl::kokkosp_begin_parallel_reduce)
EXPOSE_END_PARALLEL_REDUCE(impl::kokkosp_end_parallel_reduce)

}  // end extern "C"
