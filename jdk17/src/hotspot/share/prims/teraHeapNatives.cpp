// Natives of jdk.teraheap.TeraHeap, the TeraHeap hint API for applications.
// Bound by JVM_RegisterTeraHeapMethods(), which the class's registerNatives()
// reaches through lookup_special_native() (nativeLookup.cpp): no native
// library. sun.misc.Unsafe has the same hints (unsafe.cpp).

#include "precompiled.hpp"
#include "jni.h"
#include "jvm.h"
#include "gc/teraHeap/teraHeap.inline.hpp"
#include "memory/universe.hpp"
#include "oops/oop.inline.hpp"
#include "runtime/globals.hpp"
#include "runtime/interfaceSupport.inline.hpp"
#include "runtime/jniHandles.inline.hpp"

JVM_ENTRY(static jboolean, TeraHeap_isEnabled0(JNIEnv *env, jclass cls)) {
  return EnableTeraHeap;
} JVM_END

JVM_ENTRY(static void, TeraHeap_h2TagAndMoveRoot0(JNIEnv *env, jclass cls, jobject obj, jlong label)) {
  oop o = JNIHandles::resolve(obj);
  if (!EnableTeraHeap || o == NULL)
    return;
  Universe::teraHeap()->tag_root_for_h2(o, (uint64_t) label);
} JVM_END

JVM_ENTRY(static jboolean, TeraHeap_isInH2_0(JNIEnv *env, jclass cls, jobject obj)) {
  oop o = JNIHandles::resolve(obj);
  return EnableTeraHeap && o != NULL && Universe::teraHeap()->is_in_h2(o);
} JVM_END

JVM_ENTRY(static jboolean, TeraHeap_isMarkedMoveH2_0(JNIEnv *env, jclass cls, jobject obj)) {
  oop o = JNIHandles::resolve(obj);
  return EnableTeraHeap && o != NULL && o->is_marked_move_h2();
} JVM_END

#define CC (char*)  /* cast a literal from (const char*) */
#define FN_PTR(f) CAST_FROM_FN_PTR(void*, &f)
#define OBJ "Ljava/lang/Object;"

static JNINativeMethod jdk_teraheap_TeraHeap_methods[] = {
  {CC "isEnabled0",         CC "()Z",          FN_PTR(TeraHeap_isEnabled0)},
  {CC "h2TagAndMoveRoot0",  CC "(" OBJ "J)V",  FN_PTR(TeraHeap_h2TagAndMoveRoot0)},
  {CC "isInH2_0",           CC "(" OBJ ")Z",   FN_PTR(TeraHeap_isInH2_0)},
  {CC "isMarkedMoveH2_0",   CC "(" OBJ ")Z",   FN_PTR(TeraHeap_isMarkedMoveH2_0)},
};

#undef CC
#undef FN_PTR
#undef OBJ

JVM_ENTRY(void, JVM_RegisterTeraHeapMethods(JNIEnv *env, jclass cls)) {
  ThreadToNativeFromVM ttnfv(thread);

  int ok = env->RegisterNatives(cls, jdk_teraheap_TeraHeap_methods,
                                sizeof(jdk_teraheap_TeraHeap_methods) / sizeof(JNINativeMethod));
  guarantee(ok == 0, "register jdk.teraheap.TeraHeap natives");
} JVM_END
