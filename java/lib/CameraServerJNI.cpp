/*----------------------------------------------------------------------------*/
/* Copyright (c) FIRST 2016. All Rights Reserved.                             */
/* Open Source Software - may be modified and shared by FRC teams. The code   */
/* must be accompanied by the FIRST BSD license file in the root directory of */
/* the project.                                                               */
/*----------------------------------------------------------------------------*/

#include "edu_wpi_cscore_CameraServerJNI.h"

#include "llvm/SmallString.h"
#include "support/jni_util.h"

#include "cscore_cpp.h"

using namespace wpi::java;

//
// Globals and load/unload
//

// Used for callback.
static JavaVM *jvm = nullptr;
static jclass usbCameraInfoCls = nullptr;
static jclass videoModeCls = nullptr;
static jclass videoEventCls = nullptr;
// Thread-attached environment for listener callbacks.
static JNIEnv *listenerEnv = nullptr;

static void ListenerOnStart() {
  if (!jvm) return;
  JNIEnv *env;
  JavaVMAttachArgs args;
  args.version = JNI_VERSION_1_2;
  args.name = const_cast<char*>("CSListener");
  args.group = nullptr;
  if (jvm->AttachCurrentThreadAsDaemon(reinterpret_cast<void **>(&env),
                                       &args) != JNI_OK)
    return;
  if (!env || !env->functions) return;
  listenerEnv = env;
}

static void ListenerOnExit() {
  listenerEnv = nullptr;
  if (!jvm) return;
  jvm->DetachCurrentThread();
}

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
  jvm = vm;

  JNIEnv *env;
  if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK)
    return JNI_ERR;

  // Cache references to classes
  jclass local;

  local = env->FindClass("edu/wpi/cameraserver/USBCameraInfo");
  if (!local) return JNI_ERR;
  usbCameraInfoCls = static_cast<jclass>(env->NewGlobalRef(local));
  if (!usbCameraInfoCls) return JNI_ERR;
  env->DeleteLocalRef(local);

  local = env->FindClass("edu/wpi/cameraserver/VideoMode");
  if (!local) return JNI_ERR;
  videoModeCls = static_cast<jclass>(env->NewGlobalRef(local));
  if (!videoModeCls) return JNI_ERR;
  env->DeleteLocalRef(local);

  local = env->FindClass("edu/wpi/cameraserver/VideoEvent");
  if (!local) return JNI_ERR;
  videoEventCls = static_cast<jclass>(env->NewGlobalRef(local));
  if (!videoEventCls) return JNI_ERR;
  env->DeleteLocalRef(local);

  // Initial configuration of listener start/exit
  cs::SetListenerOnStart(ListenerOnStart);
  cs::SetListenerOnExit(ListenerOnExit);

  return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM *vm, void *reserved) {
  JNIEnv *env;
  if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK)
    return;
  // Delete global references
  if (usbCameraInfoCls) env->DeleteGlobalRef(usbCameraInfoCls);
  if (videoModeCls) env->DeleteGlobalRef(videoModeCls);
  if (videoEventCls) env->DeleteGlobalRef(videoEventCls);
  jvm = nullptr;
}

}  // extern "C"

//
// Helper class to create and clean up a global reference
//
template <typename T>
class JGlobal {
 public:
  JGlobal(JNIEnv *env, T obj)
      : m_obj(static_cast<T>(env->NewGlobalRef(obj))) {}
  ~JGlobal() {
    if (!jvm || cs::NotifierDestroyed()) return;
    JNIEnv *env;
    bool attached = false;
    // don't attach and de-attach if already attached to a thread.
    if (jvm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) ==
        JNI_EDETACHED) {
      if (jvm->AttachCurrentThread(reinterpret_cast<void **>(&env), nullptr) !=
          JNI_OK)
        return;
      attached = true;
    }
    if (!env || !env->functions) return;
    env->DeleteGlobalRef(m_obj);
    if (attached) jvm->DetachCurrentThread();
  }
  operator T() { return m_obj; }
  T obj() { return m_obj; }

 private:
  T m_obj;
};

static void ReportError(JNIEnv *env, CS_Status status, bool do_throw = true) {
  // TODO
}

static inline bool CheckStatus(JNIEnv *env, CS_Status status,
                               bool do_throw = true) {
  if (status != 0) ReportError(env, status, do_throw);
  return status == 0;
}

static jobject MakeJObject(JNIEnv *env, const cs::USBCameraInfo &info) {
  static jmethodID constructor = env->GetMethodID(
      usbCameraInfoCls, "<init>", "(ILjava/lang/String;Ljava/lang/String;)V");
  JLocal<jstring> path(env, MakeJString(env, info.path));
  JLocal<jstring> name(env, MakeJString(env, info.name));
  return env->NewObject(usbCameraInfoCls, constructor,
                        static_cast<jint>(info.dev), path.obj(), name.obj());
}

static jobject MakeJObject(JNIEnv *env, const cs::VideoMode &videoMode) {
  static jmethodID constructor =
      env->GetMethodID(videoModeCls, "<init>", "(IIII)V");
  return env->NewObject(
      videoModeCls, constructor, static_cast<jint>(videoMode.pixelFormat),
      static_cast<jint>(videoMode.width), static_cast<jint>(videoMode.height),
      static_cast<jint>(videoMode.fps));
}

static jobject MakeJObject(JNIEnv *env, const cs::RawEvent &event) {
  static jmethodID constructor =
      env->GetMethodID(videoEventCls, "<init>",
                       "(IIILjava/lang/String;IIIIIIILjava/lang/String;)V");
  JLocal<jstring> name(env, MakeJString(env, event.name));
  JLocal<jstring> valueStr(env, MakeJString(env, event.valueStr));
  return env->NewObject(
      videoEventCls,
      constructor,
      static_cast<jint>(event.type),
      static_cast<jint>(event.sourceHandle),
      static_cast<jint>(event.sinkHandle),
      name.obj(),
      static_cast<jint>(event.mode.pixelFormat),
      static_cast<jint>(event.mode.width),
      static_cast<jint>(event.mode.height),
      static_cast<jint>(event.mode.fps),
      static_cast<jint>(event.propertyHandle),
      static_cast<jint>(event.propertyType),
      static_cast<jint>(event.value),
      valueStr.obj());
}

extern "C" {

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    getPropertyType
 * Signature: (I)I
 */
JNIEXPORT jint JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_getPropertyType
  (JNIEnv *env, jclass, jint property)
{
  CS_Status status = 0;
  auto val = cs::GetPropertyType(property, &status);
  CheckStatus(env, status);
  return val;
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    getPropertyName
 * Signature: (I)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_getPropertyName
  (JNIEnv *env, jclass, jint property)
{
  CS_Status status = 0;
  llvm::SmallString<128> buf;
  auto str = cs::GetPropertyName(property, buf, &status);
  if (!CheckStatus(env, status)) return nullptr;
  return MakeJString(env, str);
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    getProperty
 * Signature: (I)I
 */
JNIEXPORT jint JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_getProperty
  (JNIEnv *env, jclass, jint property)
{
  CS_Status status = 0;
  auto val = cs::GetProperty(property, &status);
  CheckStatus(env, status);
  return val;
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    setProperty
 * Signature: (II)V
 */
JNIEXPORT void JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_setProperty
  (JNIEnv *env, jclass, jint property, jint value)
{
  CS_Status status = 0;
  cs::SetProperty(property, value, &status);
  CheckStatus(env, status);
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    getPropertyMin
 * Signature: (I)I
 */
JNIEXPORT jint JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_getPropertyMin
  (JNIEnv *env, jclass, jint property)
{
  CS_Status status = 0;
  auto val = cs::GetPropertyMin(property, &status);
  CheckStatus(env, status);
  return val;
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    getPropertyMax
 * Signature: (I)I
 */
JNIEXPORT jint JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_getPropertyMax
  (JNIEnv *env, jclass, jint property)
{
  CS_Status status = 0;
  auto val = cs::GetPropertyMax(property, &status);
  CheckStatus(env, status);
  return val;
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    getPropertyStep
 * Signature: (I)I
 */
JNIEXPORT jint JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_getPropertyStep
  (JNIEnv *env, jclass, jint property)
{
  CS_Status status = 0;
  auto val = cs::GetPropertyStep(property, &status);
  CheckStatus(env, status);
  return val;
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    getPropertyDefault
 * Signature: (I)I
 */
JNIEXPORT jint JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_getPropertyDefault
  (JNIEnv *env, jclass, jint property)
{
  CS_Status status = 0;
  auto val = cs::GetPropertyDefault(property, &status);
  CheckStatus(env, status);
  return val;
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    getStringProperty
 * Signature: (I)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_getStringProperty
  (JNIEnv *env, jclass, jint property)
{
  CS_Status status = 0;
  llvm::SmallString<128> buf;
  auto str = cs::GetStringProperty(property, buf, &status);
  if (!CheckStatus(env, status)) return nullptr;
  return MakeJString(env, str);
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    setStringProperty
 * Signature: (ILjava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_setStringProperty
  (JNIEnv *env, jclass, jint property, jstring value)
{
  CS_Status status = 0;
  cs::SetStringProperty(property, JStringRef{env, value}, &status);
  CheckStatus(env, status);
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    getEnumPropertyChoices
 * Signature: (I)[Ljava/lang/String;
 */
JNIEXPORT jobjectArray JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_getEnumPropertyChoices
  (JNIEnv *env, jclass, jint property)
{
  CS_Status status = 0;
  auto arr = cs::GetEnumPropertyChoices(property, &status);
  if (!CheckStatus(env, status)) return nullptr;
  return MakeJStringArray(env, arr);
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    createUSBCameraDev
 * Signature: (Ljava/lang/String;I)I
 */
JNIEXPORT jint JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_createUSBCameraDev
  (JNIEnv *env, jclass, jstring name, jint dev)
{
  CS_Status status = 0;
  auto val = cs::CreateUSBCameraDev(JStringRef{env, name}, dev, &status);
  CheckStatus(env, status);
  return val;
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    createUSBCameraPath
 * Signature: (Ljava/lang/String;Ljava/lang/String;)I
 */
JNIEXPORT jint JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_createUSBCameraPath
  (JNIEnv *env, jclass, jstring name, jstring path)
{
  CS_Status status = 0;
  auto val = cs::CreateUSBCameraPath(JStringRef{env, name},
                                     JStringRef{env, path}, &status);
  CheckStatus(env, status);
  return val;
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    createHTTPCamera
 * Signature: (Ljava/lang/String;Ljava/lang/String;)I
 */
JNIEXPORT jint JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_createHTTPCamera
  (JNIEnv *env, jclass, jstring name, jstring url)
{
  CS_Status status = 0;
  auto val = cs::CreateHTTPCamera(JStringRef{env, name},
                                  JStringRef{env, url}, &status);
  CheckStatus(env, status);
  return val;
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    createCvSource
 * Signature: (Ljava/lang/String;IIII)I
 */
JNIEXPORT jint JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_createCvSource
  (JNIEnv *env, jclass, jstring name, jint pixelFormat, jint width, jint height,
   jint fps)
{
  CS_Status status = 0;
  auto val = cs::CreateCvSource(
      JStringRef{env, name},
      cs::VideoMode{static_cast<cs::VideoMode::PixelFormat>(pixelFormat),
                    static_cast<int>(width), static_cast<int>(height),
                    static_cast<int>(fps)},
      &status);
  CheckStatus(env, status);
  return val;
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    getSourceName
 * Signature: (I)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_getSourceName
  (JNIEnv *env, jclass, jint source)
{
  CS_Status status = 0;
  llvm::SmallString<128> buf;
  auto str = cs::GetSourceName(source, buf, &status);
  if (!CheckStatus(env, status)) return nullptr;
  return MakeJString(env, str);
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    getSourceDescription
 * Signature: (I)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_getSourceDescription
  (JNIEnv *env, jclass, jint source)
{
  CS_Status status = 0;
  llvm::SmallString<128> buf;
  auto str = cs::GetSourceDescription(source, buf, &status);
  if (!CheckStatus(env, status)) return nullptr;
  return MakeJString(env, str);
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    getSourceLastFrameTime
 * Signature: (I)J
 */
JNIEXPORT jlong JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_getSourceLastFrameTime
  (JNIEnv *env, jclass, jint source)
{
  CS_Status status = 0;
  auto val = cs::GetSourceLastFrameTime(source, &status);
  CheckStatus(env, status);
  return val;
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    isSourceConnected
 * Signature: (I)Z
 */
JNIEXPORT jboolean JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_isSourceConnected
  (JNIEnv *env, jclass, jint source)
{
  CS_Status status = 0;
  auto val = cs::IsSourceConnected(source, &status);
  CheckStatus(env, status);
  return val;
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    getSourceProperty
 * Signature: (ILjava/lang/String;)I
 */
JNIEXPORT jint JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_getSourceProperty
  (JNIEnv *env, jclass, jint source, jstring name)
{
  CS_Status status = 0;
  auto val = cs::GetSourceProperty(source, JStringRef{env, name}, &status);
  CheckStatus(env, status);
  return val;
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    enumerateSourceProperties
 * Signature: (I)[I
 */
JNIEXPORT jintArray JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_enumerateSourceProperties
  (JNIEnv *env, jclass, jint source)
{
  CS_Status status = 0;
  llvm::SmallVector<CS_Property, 32> buf;
  auto arr = cs::EnumerateSourceProperties(source, buf, &status);
  if (!CheckStatus(env, status)) return nullptr;
  return MakeJIntArray(env, arr);
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    getSourceVideoMode
 * Signature: (I)Ledu/wpi/cameraserver/VideoMode;
 */
JNIEXPORT jobject JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_getSourceVideoMode
  (JNIEnv *env, jclass, jint source)
{
  CS_Status status = 0;
  auto val = cs::GetSourceVideoMode(source, &status);
  if (!CheckStatus(env, status)) return nullptr;
  return MakeJObject(env, val);
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    setSourceVideoMode
 * Signature: (IIIII)Z
 */
JNIEXPORT jboolean JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_setSourceVideoMode
  (JNIEnv *env, jclass, jint source, jint pixelFormat, jint width, jint height,
   jint fps)
{
  CS_Status status = 0;
  auto val = cs::SetSourceVideoMode(
      source,
      cs::VideoMode(static_cast<cs::VideoMode::PixelFormat>(pixelFormat), width,
                    height, fps),
      &status);
  CheckStatus(env, status);
  return val;
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    setSourcePixelFormat
 * Signature: (II)Z
 */
JNIEXPORT jboolean JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_setSourcePixelFormat
  (JNIEnv *env, jclass, jint source, jint pixelFormat)
{
  CS_Status status = 0;
  auto val = cs::SetSourcePixelFormat(
      source, static_cast<cs::VideoMode::PixelFormat>(pixelFormat), &status);
  CheckStatus(env, status);
  return val;
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    setSourceResolution
 * Signature: (III)Z
 */
JNIEXPORT jboolean JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_setSourceResolution
  (JNIEnv *env, jclass, jint source, jint width, jint height)
{
  CS_Status status = 0;
  auto val = cs::SetSourceResolution(source, width, height, &status);
  CheckStatus(env, status);
  return val;
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    setSourceFPS
 * Signature: (II)Z
 */
JNIEXPORT jboolean JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_setSourceFPS
  (JNIEnv *env, jclass, jint source, jint fps)
{
  CS_Status status = 0;
  auto val = cs::SetSourceFPS(source, fps, &status);
  CheckStatus(env, status);
  return val;
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    enumerateSourceVideoModes
 * Signature: (I)[Ledu/wpi/cameraserver/VideoMode;
 */
JNIEXPORT jobjectArray JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_enumerateSourceVideoModes
  (JNIEnv *env, jclass, jint source)
{
  CS_Status status = 0;
  auto arr = cs::EnumerateSourceVideoModes(source, &status);
  if (!CheckStatus(env, status)) return nullptr;
  jobjectArray jarr =
      env->NewObjectArray(arr.size(), videoModeCls, nullptr);
  if (!jarr) return nullptr;
  for (size_t i = 0; i < arr.size(); ++i) {
    JLocal<jobject> jelem{env, MakeJObject(env, arr[i])};
    env->SetObjectArrayElement(jarr, i, jelem);
  }
  return jarr;
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    copySource
 * Signature: (I)I
 */
JNIEXPORT jint JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_copySource
  (JNIEnv *env, jclass, jint source)
{
  CS_Status status = 0;
  auto val = cs::CopySource(source, &status);
  CheckStatus(env, status);
  return val;
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    releaseSource
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_releaseSource
  (JNIEnv *env, jclass, jint source)
{
  CS_Status status = 0;
  cs::ReleaseSource(source, &status);
  CheckStatus(env, status);
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    putSourceFrame
 * Signature: (IJ)V
 */
JNIEXPORT void JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_putSourceFrame
  (JNIEnv *env, jclass, jint source, jlong imageNativeObj)
{
  cv::Mat& image = *((cv::Mat*)imageNativeObj);
  CS_Status status = 0;
  cs::PutSourceFrame(source, image, &status);
  CheckStatus(env, status);
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    notifySourceError
 * Signature: (ILjava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_notifySourceError
  (JNIEnv *env, jclass, jint source, jstring msg)
{
  CS_Status status = 0;
  cs::NotifySourceError(source, JStringRef{env, msg}, &status);
  CheckStatus(env, status);
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    setSourceConnected
 * Signature: (IZ)V
 */
JNIEXPORT void JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_setSourceConnected
  (JNIEnv *env, jclass, jint source, jboolean connected)
{
  CS_Status status = 0;
  cs::SetSourceConnected(source, connected, &status);
  CheckStatus(env, status);
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    setSourceDescription
 * Signature: (ILjava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_setSourceDescription
  (JNIEnv *env, jclass, jint source, jstring description)
{
  CS_Status status = 0;
  cs::SetSourceDescription(source, JStringRef{env, description}, &status);
  CheckStatus(env, status);
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    createSourceProperty
 * Signature: (ILjava/lang/String;IIIIII)I
 */
JNIEXPORT jint JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_createSourceProperty
  (JNIEnv *env, jclass, jint source, jstring name, jint type, jint minimum, jint maximum, jint step, jint defaultValue, jint value)
{
  CS_Status status = 0;
  auto val = cs::CreateSourceProperty(
      source, JStringRef{env, name}, static_cast<CS_PropertyType>(type),
      minimum, maximum, step, defaultValue, value, &status);
  CheckStatus(env, status);
  return val;
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    setSourceEnumPropertyChoices
 * Signature: (II[Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_setSourceEnumPropertyChoices
  (JNIEnv *env, jclass, jint source, jint property, jobjectArray choices)
{
  size_t len = env->GetArrayLength(choices);
  llvm::SmallVector<std::string, 8> vec;
  vec.reserve(len);
  for (size_t i = 0; i < len; ++i) {
    JLocal<jstring> elem{
        env, static_cast<jstring>(env->GetObjectArrayElement(choices, i))};
    if (!elem) {
      // TODO
      return;
    }
    vec.push_back(JStringRef{env, elem}.str());
  }
  CS_Status status = 0;
  cs::SetSourceEnumPropertyChoices(source, property, vec, &status);
  CheckStatus(env, status);
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    createMJPEGServer
 * Signature: (Ljava/lang/String;Ljava/lang/String;I)I
 */
JNIEXPORT jint JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_createMJPEGServer
  (JNIEnv *env, jclass, jstring name, jstring listenAddress, jint port)
{
  CS_Status status = 0;
  auto val = cs::CreateMJPEGServer(
      JStringRef{env, name}, JStringRef{env, listenAddress}, port, &status);
  CheckStatus(env, status);
  return val;
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    createCvSink
 * Signature: (Ljava/lang/String;)I
 */
JNIEXPORT jint JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_createCvSink
  (JNIEnv *env, jclass, jstring name)
{
  CS_Status status = 0;
  auto val = cs::CreateCvSink(JStringRef{env, name}, &status);
  CheckStatus(env, status);
  return val;
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    getSinkName
 * Signature: (I)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_getSinkName
  (JNIEnv *env, jclass, jint sink)
{
  CS_Status status = 0;
  llvm::SmallString<128> buf;
  auto str = cs::GetSinkName(sink, buf, &status);
  if (!CheckStatus(env, status)) return nullptr;
  return MakeJString(env, str);
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    getSinkDescription
 * Signature: (I)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_getSinkDescription
  (JNIEnv *env, jclass, jint sink)
{
  CS_Status status = 0;
  llvm::SmallString<128> buf;
  auto str = cs::GetSinkDescription(sink, buf, &status);
  if (!CheckStatus(env, status)) return nullptr;
  return MakeJString(env, str);
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    setSinkSource
 * Signature: (II)V
 */
JNIEXPORT void JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_setSinkSource
  (JNIEnv *env, jclass, jint sink, jint source)
{
  CS_Status status = 0;
  cs::SetSinkSource(sink, source, &status);
  CheckStatus(env, status);
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    getSinkSourceProperty
 * Signature: (ILjava/lang/String;)I
 */
JNIEXPORT jint JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_getSinkSourceProperty
  (JNIEnv *env, jclass, jint sink, jstring name)
{
  CS_Status status = 0;
  auto val = cs::GetSinkSourceProperty(sink, JStringRef{env, name}, &status);
  CheckStatus(env, status);
  return val;
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    getSinkSource
 * Signature: (I)I
 */
JNIEXPORT jint JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_getSinkSource
  (JNIEnv *env, jclass, jint sink)
{
  CS_Status status = 0;
  auto val = cs::GetSinkSource(sink, &status);
  CheckStatus(env, status);
  return val;
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    copySink
 * Signature: (I)I
 */
JNIEXPORT jint JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_copySink
  (JNIEnv *env, jclass, jint sink)
{
  CS_Status status = 0;
  auto val = cs::CopySink(sink, &status);
  CheckStatus(env, status);
  return val;
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    releaseSink
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_releaseSink
  (JNIEnv *env, jclass, jint sink)
{
  CS_Status status = 0;
  cs::ReleaseSink(sink, &status);
  CheckStatus(env, status);
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    setSinkDescription
 * Signature: (ILjava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_setSinkDescription
  (JNIEnv *env, jclass, jint sink, jstring description)
{
  CS_Status status = 0;
  cs::SetSinkDescription(sink, JStringRef{env, description}, &status);
  CheckStatus(env, status);
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    grabSinkFrame
 * Signature: (IJ)J
 */
JNIEXPORT jlong JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_grabSinkFrame
  (JNIEnv *env, jclass, jint sink, jlong imageNativeObj)
{
  cv::Mat& image = *((cv::Mat*)imageNativeObj);
  CS_Status status = 0;
  auto rv = cs::GrabSinkFrame(sink, image, &status);
  CheckStatus(env, status);
  return rv;
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    getSinkError
 * Signature: (I)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_getSinkError
  (JNIEnv *env, jclass, jint sink)
{
  CS_Status status = 0;
  llvm::SmallString<128> buf;
  auto str = cs::GetSinkError(sink, buf, &status);
  if (!CheckStatus(env, status)) return nullptr;
  return MakeJString(env, str);
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    setSinkEnabled
 * Signature: (IZ)V
 */
JNIEXPORT void JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_setSinkEnabled
  (JNIEnv *env, jclass, jint sink, jboolean enabled)
{
  CS_Status status = 0;
  cs::SetSinkEnabled(sink, enabled, &status);
  CheckStatus(env, status);
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    addListener
 * Signature: (Ledu/wpi/cameraserver/CameraServerJNI/ConnectionListenerFunction;IZ)I
 */
JNIEXPORT jint JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_addListener
  (JNIEnv *envouter, jclass, jobject listener, jint eventMask, jboolean immediateNotify)
{
  // the shared pointer to the weak global will keep it around until the
  // entry listener is destroyed
  auto listener_global =
      std::make_shared<JGlobal<jobject>>(envouter, listener);

  // cls is a temporary here; cannot be used within callback functor
	jclass cls = envouter->GetObjectClass(listener);
  if (!cls) return 0;

  // method ids, on the other hand, are safe to retain
  jmethodID mid = envouter->GetMethodID(cls, "apply",
                                        "(Ledu/wpi/cameraserver/VideoEvent;)V");
  if (!mid) return 0;

  CS_Status status = 0;
  CS_Listener handle = cs::AddListener(
      [=](const cs::RawEvent &event) {
        JNIEnv *env = listenerEnv;
        if (!env || !env->functions) return;

        // get the handler
        auto handler = listener_global->obj();

        // convert into the appropriate Java type
        JLocal<jobject> jobj{env, MakeJObject(env, event)};
        if (env->ExceptionCheck()) {
          env->ExceptionDescribe();
          env->ExceptionClear();
          return;
        }
        if (!jobj) return;

        env->CallVoidMethod(handler, mid, jobj.obj());
        if (env->ExceptionCheck()) {
          env->ExceptionDescribe();
          env->ExceptionClear();
        }
      },
      eventMask, immediateNotify != JNI_FALSE, &status);
  CheckStatus(envouter, status);
  return handle;
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    removeListener
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_removeListener
  (JNIEnv *env, jclass, jint handle)
{
  CS_Status status = 0;
  cs::RemoveListener(handle, &status);
  CheckStatus(env, status);
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    enumerateUSBCameras
 * Signature: ()[Ledu/wpi/cameraserver/USBCameraInfo;
 */
JNIEXPORT jobjectArray JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_enumerateUSBCameras
  (JNIEnv *env, jclass)
{
  CS_Status status = 0;
  auto arr = cs::EnumerateUSBCameras(&status);
  if (!CheckStatus(env, status)) return nullptr;
  jobjectArray jarr =
      env->NewObjectArray(arr.size(), usbCameraInfoCls, nullptr);
  if (!jarr) return nullptr;
  for (size_t i = 0; i < arr.size(); ++i) {
    JLocal<jobject> jelem{env, MakeJObject(env, arr[i])};
    env->SetObjectArrayElement(jarr, i, jelem);
  }
  return jarr;
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    enumerateSources
 * Signature: ()[I
 */
JNIEXPORT jintArray JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_enumerateSources
  (JNIEnv *env, jclass)
{
  CS_Status status = 0;
  llvm::SmallVector<CS_Source, 16> buf;
  auto arr = cs::EnumerateSourceHandles(buf, &status);
  if (!CheckStatus(env, status)) return nullptr;
  return MakeJIntArray(env, arr);
}

/*
 * Class:     edu_wpi_cameraserver_CameraServerJNI
 * Method:    enumerateSinks
 * Signature: ()[I
 */
JNIEXPORT jintArray JNICALL Java_edu_wpi_cameraserver_CameraServerJNI_enumerateSinks
  (JNIEnv *env, jclass)
{
  CS_Status status = 0;
  llvm::SmallVector<CS_Sink, 16> buf;
  auto arr = cs::EnumerateSinkHandles(buf, &status);
  if (!CheckStatus(env, status)) return nullptr;
  return MakeJIntArray(env, arr);
}

}  // extern "C"
