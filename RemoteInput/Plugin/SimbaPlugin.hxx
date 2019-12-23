#ifndef SIMBAPLUGIN_HXX_INCLUDED
#define SIMBAPLUGIN_HXX_INCLUDED

#include <cstdint>
#include <cstdio>

#include "EIOS.hxx"
#include "TMemoryManager.hxx"


static const char* PascalExports[] =
{
    "Pascal_Reflect_GetEIOS", "Function RIGetEIOS(pid: Int32): Pointer; native;",
	"Pascal_Reflect_Object", "Function RIGetObject(eios: Pointer; instance: Pointer; constref field: ^const RIField): Pointer; native;",
    "Pascal_Reflect_Release_Object", "Procedure RIReleaseObject(eios: Pointer; instance: Pointer); native;",
    "Pascal_Reflect_Release_Objects", "Procedure RIReleaseObjects(eios: Pointer; objects: Array of Pointer); native;",
    "Pascal_Reflect_Boolean", "Function RIGetBool(eios: Pointer; instance: Pointer; constref field: ^const RIField): Boolean; native;",
    "Pascal_Reflect_Char", "Function RIGetChar(eios: Pointer; instance: Pointer; constref field: ^const RIField): Char; native;",
    "Pascal_Reflect_Byte", "Function RIGetByte(eios: Pointer; instance: Pointer; constref field: ^const RIField): Byte; native;",
    "Pascal_Reflect_Short", "Function RIGetShort(eios: Pointer; instance: Pointer; constref field: ^const RIField): Int16; native;",
    "Pascal_Reflect_Int", "Function RIGetInt(eios: Pointer; instance: Pointer; constref field: ^const RIField): Int32; native;",
    "Pascal_Reflect_Long", "Function RIGetLong(eios: Pointer; instance: Pointer; constref field: ^const RIField): Int64; native;",
    "Pascal_Reflect_Float", "Function RIGetFloat(eios: Pointer; instance: Pointer; constref field: ^const RIField): Single; native;",
    "Pascal_Reflect_Double", "Function RIGetDouble(eios: Pointer; instance: Pointer; constref field: ^const RIField): Double; native;",
    "Pascal_Reflect_String", "Function RIGetString(eios: Pointer; instance: Pointer; constref field: ^const RIField): String; native;",
	"Pascal_Reflect_Array", "Function RIGetArray(eios: Pointer; instance: Pointer; constref field: ^const RIField): Pointer; native;",

    "Pascal_Reflect_Array_With_Size", "Function RIGetArray(eios: Pointer; instance: Pointer; output_size: ^SizeUInt; constref field: ^const RIField): Pointer; overload; native;",
    "Pascal_Reflect_Array_Size", "Function RIGetArraySize(eios: Pointer; arr: Pointer): SizeUInt; native;",
    "Pascal_Reflect_Array_Index", "Function RIGetArrayElement(eios: Pointer; arr: Pointer; elementType: ReflectionArrayType; index: SizeUInt; length: SizeUInt): Pointer; native;",
    "Pascal_Reflect_Array_Index2D", "Function RIGetArrayElement(eios: Pointer; arr: Pointer; elementType: ReflectionArrayType; length: SizeUInt; x: Int32; y: Int32): Pointer; overload; native;",
    "Pascal_Reflect_Array_Index3D", "Function RIGetArrayElement(eios: Pointer; arr: Pointer; elementType: ReflectionArrayType; length: SizeUInt; x: Int32; y: Int32; z: Int32): Pointer; overload; native;",
    "Pascal_Reflect_Array_Index4D", "Function RIGetArrayElement(eios: Pointer; arr: Pointer; elementType: ReflectionArrayType; length: SizeUInt; x: Int32; y: Int32; z: Int32; w: Int32): Pointer; overload; native;",

	"Pascal_GetDebugImageBuffer", "Function EIOS_GetDebugImageBuffer(eios: Pointer): ^UInt8; native;"
};

static const char* PascalTypes[] =
{
	"ReflectionArrayType", "(RI_CHAR = 0, RI_BYTE = 1, RI_BOOLEAN = 2, RI_SHORT = 3, RI_INT = 4, RI_LONG = 5, RI_FLOAT = 6, RI_DOUBLE = 7, RI_STRING = 8, RI_OBJECT = 9)",
	"RIField", "packed record cls, field, desc: String; end;"
};

static const long int PascalExportCount = sizeof(PascalExports) / (sizeof(PascalExports[0]) * 2);
static const long int PascalTypeCount = sizeof(PascalTypes) / (sizeof(PascalTypes[0]) * 2);


#ifdef __cplusplus
extern "C"
{
#endif

EXPORT int GetPluginABIVersion();
EXPORT int GetFunctionCount();
EXPORT int GetTypeCount();
EXPORT int GetFunctionInfo(int Index, void** Address, char** Definition);
EXPORT int GetTypeInfo(int Index, char** Type, char** Definition);

EXPORT void SetPluginMemManager(TMemoryManager MemMgr);
EXPORT void OnAttach(void* info);
EXPORT void OnDetach();

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C"
{
#endif

EXPORT void Pascal_Reflect_GetEIOS(void** Params, void** Result);
EXPORT void Pascal_Reflect_Object(void** Params, void** Result);
EXPORT void Pascal_Reflect_Release_Object(void** Params, void** Result);
EXPORT void Pascal_Reflect_Release_Objects(void** Params, void** Result);
EXPORT void Pascal_Reflect_Char(void** Params, void** Result);
EXPORT void Pascal_Reflect_Byte(void** Params, void** Result);
EXPORT void Pascal_Reflect_Short(void** Params, void** Result);
EXPORT void Pascal_Reflect_Int(void** Params, void** Result);
EXPORT void Pascal_Reflect_Long(void** Params, void** Result);
EXPORT void Pascal_Reflect_Float(void** Params, void** Result);
EXPORT void Pascal_Reflect_Double(void** Params, void** Result);
EXPORT void Pascal_Reflect_String(void** Params, void** Result);
EXPORT void Pascal_Reflect_Array(void** Params, void** Result);
EXPORT void Pascal_Reflect_Array_With_Size(void** Params, void** Result);
EXPORT void Pascal_Reflect_Array_Size(void** Params, void** Result);
EXPORT void Pascal_Reflect_Array_Index(void** Params, void** Result);
EXPORT void Pascal_Reflect_Array_Index2D(void** Params, void** Result);
EXPORT void Pascal_Reflect_Array_Index3D(void** Params, void** Result);
EXPORT void Pascal_Reflect_Array_Index4D(void** Params, void** Result);
EXPORT void Pascal_Pascal_GetDebugImageBuffer(void** Params, void** Result);

#ifdef __cplusplus
}
#endif

#endif // SIMBAPLUGIN_HXX_INCLUDED
