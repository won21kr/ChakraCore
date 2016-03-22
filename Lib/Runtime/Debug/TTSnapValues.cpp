//-------------------------------------------------------------------------------------------------------
// Copyright (C) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------
#include "RuntimeDebugPch.h"

#if ENABLE_TTD

namespace TTD
{
    namespace JsSupport
    {
        bool IsVarTaggedInline(Js::Var v)
        {
            return Js::TaggedNumber::Is(v);
        }

        bool IsVarPtrValued(Js::Var v)
        {
            return !Js::TaggedNumber::Is(v);
        }

        bool IsVarPrimitiveKind(Js::Var v)
        {
            if(Js::TaggedNumber::Is(v))
            {
                return false;
            }

            Js::TypeId tid = Js::RecyclableObject::FromVar(v)->GetTypeId();
            return tid <= Js::TypeIds_LastToPrimitiveType;
        }

        bool IsVarComplexKind(Js::Var v)
        {
            if(Js::TaggedNumber::Is(v))
            {
                return false;
            }

            Js::TypeId tid = Js::RecyclableObject::FromVar(v)->GetTypeId();
            return tid > Js::TypeIds_LastToPrimitiveType;
        }

        Js::FunctionBody* ForceAndGetFunctionBody(Js::ParseableFunctionInfo* pfi)
        {
            Js::FunctionBody* fb = nullptr;

            if(pfi->IsDeferredDeserializeFunction())
            {
                Js::DeferDeserializeFunctionInfo* deferDeserializeInfo = pfi->GetDeferDeserializeFunctionInfo();
                fb = deferDeserializeInfo->Deserialize();
            }
            else
            {
                if(pfi->IsDeferredParseFunction())
                {
                    fb = pfi->GetParseableFunctionInfo()->Parse();
                }
                else
                {
                    fb = pfi->GetFunctionBody();
                }
            }
            AssertMsg(fb != nullptr, "I just want a function body!!!");

            fb->EnsureDeserialized();
            return fb;
        }

        void CopyStringToHeapAllocator(LPCWSTR string, TTString& into)
        {
            if(string == nullptr)
            {
                into.Length = 0;
                into.Contents = nullptr;
            }
            else
            {
                CopyStringToHeapAllocatorWLength(string, (uint32)wcslen(string), into);
            }
        }

        void CopyStringToHeapAllocatorWLength(LPCWSTR string, uint32 length, TTString& into)
        {
            AssertMsg(string != nullptr, "Not allowed with string + length");

            into.Length = length;

            if(length == 0)
            {
                into.Contents = nullptr;
            }
            else
            {
                into.Contents = HeapNewArray(wchar, into.Length + 1);
                js_memcpy_s(into.Contents, into.Length * sizeof(wchar), string, length * sizeof(wchar));
                into.Contents[into.Length] = '\0';
            }
        }

        void DeleteStringFromHeapAllocator(TTString& string)
        {
            if(string.Contents != nullptr)
            {
                HeapDeleteArray(string.Length + 1, string.Contents);
            }
        }

        void WriteCodeToFile(IOStreamFunctions& streamFunctions, LPCWSTR srcDir, LPCWSTR docId, LPCWSTR sourceUri, const wchar* sourceBuffer, uint32 length)
        {
            HANDLE srcStream = streamFunctions.pfGetSrcCodeStream(srcDir, docId, sourceUri, false, true);
            byte* srcBuff = (byte*)HeapNewArray(byte, length);

            for(uint32 i = 0; i < length; ++i)
            {
                srcBuff[i] = (byte)sourceBuffer[i];
            }

            DWORD writtenCount = 0;
            BOOL ok = streamFunctions.pfWriteBytesToStream(srcStream, srcBuff, length, &writtenCount);
            AssertMsg(ok && writtenCount == length, "Write Failed!!!");

            HeapDeleteArray(length, srcBuff);
            streamFunctions.pfFlushAndCloseStream(srcStream, false, true);
        }

        void ReadCodeFromFile(IOStreamFunctions& streamFunctions, LPCWSTR srcDir, LPCWSTR docId, LPCWSTR sourceUri, wchar* sourceBuffer, uint32 length)
        {
            HANDLE srcStream = streamFunctions.pfGetSrcCodeStream(srcDir, docId, sourceUri, true, false);
            byte* srcBuff = (byte*)HeapNewArray(byte, length);

            DWORD readCount = 0;
            BOOL ok = streamFunctions.pfReadBytesFromStream(srcStream, srcBuff, length, &readCount);
            AssertMsg(ok && readCount == length, "Read Failed!!!");

            for(uint32 i = 0; i < length; ++i)
            {
                sourceBuffer[i] = (wchar)srcBuff[i];
            }
            sourceBuffer[length] = L'\0';

            HeapDeleteArray(length, srcBuff);
            streamFunctions.pfFlushAndCloseStream(srcStream, true, false);
        }
    }

    namespace NSSnapValues
    {
        void EmitTTDVar(TTDVar var, FileWriter* writer, NSTokens::Separator separator)
        {
            writer->WriteRecordStart(separator);

            if(var == nullptr)
            {
                writer->WriteTag<TTDVarEmitTag>(NSTokens::Key::ttdVarTag, TTDVarEmitTag::TTDVarNull);
                writer->WriteNull(NSTokens::Key::nullVal, NSTokens::Separator::CommaSeparator);
            }
            else if(Js::TaggedNumber::Is(var))
            {
#if FLOATVAR
                if(Js::TaggedInt::Is(var))
                {
#endif
                    writer->WriteTag<TTDVarEmitTag>(NSTokens::Key::ttdVarTag, TTDVarEmitTag::TTDVarInt);
                    writer->WriteInt32(NSTokens::Key::i32Val, Js::TaggedInt::ToInt32(var), NSTokens::Separator::CommaSeparator);
#if FLOATVAR
                }
                else
                {
                    AssertMsg(Js::JavascriptNumber::Is_NoTaggedIntCheck(var), "Only other tagged value we support!!!");

                    writer->WriteTag<TTDVarEmitTag>(NSTokens::Key::ttdVarTag, TTDVarEmitTag::TTDVarDouble);
                    writer->WriteDouble(NSTokens::Key::doubleVal, Js::JavascriptNumber::GetValue(var), NSTokens::Separator::CommaSeparator);
                }
#endif
            }
            else
            {
                writer->WriteTag<TTDVarEmitTag>(NSTokens::Key::ttdVarTag, TTDVarEmitTag::TTDVarAddr);
                writer->WriteAddr(NSTokens::Key::ptrIdVal, TTD_CONVERT_VAR_TO_PTR_ID(var), NSTokens::Separator::CommaSeparator);
            }

            writer->WriteRecordEnd();
        }

        TTDVar ParseTTDVar(bool readSeperator, FileReader* reader)
        {
            reader->ReadRecordStart(readSeperator);

            TTDVar res = nullptr;
            TTDVarEmitTag tag = reader->ReadTag<TTDVarEmitTag>(NSTokens::Key::ttdVarTag);
            if(tag == TTDVarEmitTag::TTDVarNull)
            {
                reader->ReadNull(NSTokens::Key::nullVal, true);
                res = nullptr;
            }
            else if(tag == TTDVarEmitTag::TTDVarInt)
            {
                int32 intVal = reader->ReadInt32(NSTokens::Key::i32Val, true);
                res = Js::TaggedInt::ToVarUnchecked(intVal);
            }
#if FLOATVAR
            else if(tag == TTDVarEmitTag::TTDVarDouble)
            {
                double doubleVal = reader->ReadDouble(NSTokens::Key::doubleVal, true);
                res = Js::JavascriptNumber::NewInlined(doubleVal, nullptr);
            }
#endif
            else
            {
                AssertMsg(tag == TTDVarEmitTag::TTDVarAddr, "Is there something else?");

                TTD_PTR_ID addrVal = reader->ReadAddr(NSTokens::Key::ptrIdVal, true);
                res = TTD_COERCE_PTR_ID_TO_VAR(addrVal);
            }

            reader->ReadRecordEnd();

            return res;
        }

#if ENABLE_SNAPSHOT_COMPARE
        void AssertSnapEquivTTDVar_Helper(const TTDVar v1, const TTDVar v2, TTDCompareMap& compareMap, TTDComparePath::StepKind stepKind, const TTDComparePath::PathEntry& next)
        {
            if(v1 == nullptr || v2 == nullptr)
            {
                compareMap.DiagnosticAssert(v1 == v2);
            }
            else if(Js::TaggedNumber::Is(v1) || Js::TaggedNumber::Is(v2))
            {
                compareMap.DiagnosticAssert(Js::TaggedNumber::Is(v1) && Js::TaggedNumber::Is(v2));

#if FLOATVAR
                if(Js::TaggedInt::Is(v1))
                {
#endif
                    compareMap.DiagnosticAssert(Js::TaggedInt::ToInt32(v1) == Js::TaggedInt::ToInt32(v2));
#if FLOATVAR
                }
                else
                {
                    if(Js::JavascriptNumber::IsNan(Js::JavascriptNumber::GetValue(v1)) || Js::JavascriptNumber::IsNan(Js::JavascriptNumber::GetValue(v2)))
                    {
                        compareMap.DiagnosticAssert(Js::JavascriptNumber::IsNan(Js::JavascriptNumber::GetValue(v1)) && Js::JavascriptNumber::IsNan(Js::JavascriptNumber::GetValue(v2)));
                    }
                    else
                    {
                        compareMap.DiagnosticAssert(Js::JavascriptNumber::GetValue(v1) == Js::JavascriptNumber::GetValue(v2));
                    }
                }
#endif
            }
            else
            {
                compareMap.CheckConsistentAndAddPtrIdMapping_Helper(TTD_CONVERT_VAR_TO_PTR_ID(v1), TTD_CONVERT_VAR_TO_PTR_ID(v2), stepKind, next);
            }
        }

        void AssertSnapEquivTTDVar_Property(const TTDVar v1, const TTDVar v2, TTDCompareMap& compareMap, Js::PropertyId pid)
        {
            TTDComparePath::PathEntry next{ (uint32)pid, nullptr };
            AssertSnapEquivTTDVar_Helper(v1, v2, compareMap, TTDComparePath::StepKind::PropertyData, next);
        }

        void AssertSnapEquivTTDVar_PropertyGetter(const TTDVar v1, const TTDVar v2, TTDCompareMap& compareMap, Js::PropertyId pid)
        {
            TTDComparePath::PathEntry next{ (uint32)pid, nullptr };
            AssertSnapEquivTTDVar_Helper(v1, v2, compareMap, TTDComparePath::StepKind::PropertyGetter, next);
        }

        void AssertSnapEquivTTDVar_PropertySetter(const TTDVar v1, const TTDVar v2, TTDCompareMap& compareMap, Js::PropertyId pid)
        {
            TTDComparePath::PathEntry next{ (uint32)pid, nullptr };
            AssertSnapEquivTTDVar_Helper(v1, v2, compareMap, TTDComparePath::StepKind::PropertySetter, next);
        }

        void AssertSnapEquivTTDVar_Array(const TTDVar v1, const TTDVar v2, TTDCompareMap& compareMap, uint32 index)
        {
            TTDComparePath::PathEntry next{ index, nullptr };
            AssertSnapEquivTTDVar_Helper(v1, v2, compareMap, TTDComparePath::StepKind::Array, next);
        }

        void AssertSnapEquivTTDVar_SlotArray(const TTDVar v1, const TTDVar v2, TTDCompareMap& compareMap, uint32 index)
        {
            TTDComparePath::PathEntry next{ index, nullptr };
            AssertSnapEquivTTDVar_Helper(v1, v2, compareMap, TTDComparePath::StepKind::SlotArray, next);
        }

        void AssertSnapEquivTTDVar_Special(const TTDVar v1, const TTDVar v2, TTDCompareMap& compareMap, LPCWSTR specialField)
        {
            TTDComparePath::PathEntry next{ -1, specialField };
            AssertSnapEquivTTDVar_Helper(v1, v2, compareMap, TTDComparePath::StepKind::Special, next);
        }

        void AssertSnapEquivTTDVar_SpecialArray(const TTDVar v1, const TTDVar v2, TTDCompareMap& compareMap, LPCWSTR specialField, uint32 index)
        {
            TTDComparePath::PathEntry next{ index, specialField };
            AssertSnapEquivTTDVar_Helper(v1, v2, compareMap, TTDComparePath::StepKind::SpecialArray, next);
        }
#endif

        //////////////////

        void ExtractSnapPrimitiveValue(SnapPrimitiveValue* snapValue, Js::RecyclableObject* jsValue, bool isWellKnown, bool isLogged, const TTDIdentifierDictionary<TTD_PTR_ID, NSSnapType::SnapType*>& idToTypeMap, SlabAllocator& alloc)
        {
            snapValue->PrimitiveValueId = TTD_CONVERT_VAR_TO_PTR_ID(jsValue);

            Js::Type* objType = jsValue->GetType();
            snapValue->SnapType = idToTypeMap.LookupKnownItem(TTD_CONVERT_TYPEINFO_TO_PTR_ID(objType));

            snapValue->ValueLogTag = isLogged ? jsValue->GetScriptContext()->GetThreadContext()->TTDInfo->LookupTagForObject(jsValue) : TTD_INVALID_LOG_TAG;

            snapValue->OptWellKnownToken = alloc.CopyRawNullTerminatedStringInto(isWellKnown ? jsValue->GetScriptContext()->ResolveKnownTokenForPrimitive_TTD(jsValue) : TTD_INVALID_WELLKNOWN_TOKEN);

            if(snapValue->OptWellKnownToken == TTD_INVALID_WELLKNOWN_TOKEN)
            {
                Js::JavascriptLibrary* jslib = jsValue->GetLibrary();

                switch(snapValue->SnapType->JsTypeId)
                {
                case Js::TypeIds_Undefined:
                case Js::TypeIds_Null:
                    break;
                case Js::TypeIds_Boolean:
                    snapValue->u_boolValue = Js::JavascriptBoolean::FromVar(jsValue)->GetValue();
                    break;
                case Js::TypeIds_Number:
                    snapValue->u_doubleValue = Js::JavascriptNumber::GetValue(jsValue);
                    break;
                case Js::TypeIds_Int64Number:
                    snapValue->u_int64Value = Js::JavascriptInt64Number::FromVar(jsValue)->GetValue();
                    break;
                case Js::TypeIds_UInt64Number:
                    snapValue->u_uint64Value = Js::JavascriptUInt64Number::FromVar(jsValue)->GetValue();
                    break;
                case Js::TypeIds_String:
                    snapValue->m_optStringValue = alloc.SlabAllocateStruct<TTString>();
                    alloc.CopyStringIntoWLength(Js::JavascriptString::FromVar(jsValue)->GetSz(), Js::JavascriptString::FromVar(jsValue)->GetLength(), *(snapValue->m_optStringValue));
                    break;
                case Js::TypeIds_Symbol:
                    snapValue->u_propertyIdValue = jslib->ExtractPrimitveSymbolId_TTD(jsValue);
                    break;
                default:
                    AssertMsg(false, "These are supposed to be primitive values on the heap e.g., no pointers or properties.");
                    break;
                }
            }
        }

        void InflateSnapPrimitiveValue(const SnapPrimitiveValue* snapValue, InflateMap* inflator)
        {
            Js::ScriptContext* ctx = inflator->LookupScriptContext(snapValue->SnapType->ScriptContextTag);
            Js::JavascriptLibrary* jslib = ctx->GetLibrary();

            Js::Var res = nullptr;
            if(snapValue->OptWellKnownToken != TTD_INVALID_WELLKNOWN_TOKEN)
            {
                res = ctx->LookupPrimitiveForKnownToken_TTD(snapValue->OptWellKnownToken);
            }
            else
            {
                Js::RecyclableObject* rObj = inflator->FindReusableObjectIfExists(snapValue->PrimitiveValueId);
                if(rObj != nullptr)
                {
                    res = rObj;
                }
                else
                {
                    switch(snapValue->SnapType->JsTypeId)
                    {
                    case Js::TypeIds_Undefined:
                        res = jslib->GetUndefined();
                        break;
                    case Js::TypeIds_Null:
                        res = jslib->GetNull();
                        break;
                    case Js::TypeIds_Boolean:
                        res = (snapValue->u_boolValue ? jslib->GetTrue() : jslib->GetFalse());
                        break;
                    case Js::TypeIds_Number:
                        res = Js::JavascriptNumber::ToVarWithCheck(snapValue->u_doubleValue, ctx);
                        break;
                    case Js::TypeIds_Int64Number:
                        res = Js::JavascriptInt64Number::ToVar(snapValue->u_int64Value, ctx);
                        break;
                    case Js::TypeIds_UInt64Number:
                        res = Js::JavascriptUInt64Number::ToVar(snapValue->u_uint64Value, ctx);
                        break;
                    case Js::TypeIds_String:
                        res = Js::JavascriptString::NewCopyBuffer(snapValue->m_optStringValue->Contents, snapValue->m_optStringValue->Length, ctx);
                        break;
                    case Js::TypeIds_Symbol:
                        res = jslib->CreatePrimitveSymbol_TTD(snapValue->u_propertyIdValue);
                        break;
                    default:
                        AssertMsg(false, "These are supposed to be primitive values e.g., no pointers or properties.");
                        res = nullptr;
                    }
                }
            }

            Js::RecyclableObject* resObj = Js::RecyclableObject::FromVar(res);

            ctx->GetThreadContext()->TTDInfo->SetObjectTrackingTagSnapAndInflate_TTD(snapValue->ValueLogTag, resObj);

            inflator->AddObject(snapValue->PrimitiveValueId, resObj);
        }

        void EmitSnapPrimitiveValue(const SnapPrimitiveValue* snapValue, FileWriter* writer, NSTokens::Separator separator)
        {
            writer->WriteRecordStart(separator);
            writer->WriteAddr(NSTokens::Key::primitiveId, snapValue->PrimitiveValueId);
            writer->WriteAddr(NSTokens::Key::typeId, snapValue->SnapType->TypePtrId, NSTokens::Separator::CommaSeparator);
            writer->WriteLogTag(NSTokens::Key::logTag, snapValue->ValueLogTag, NSTokens::Separator::CommaSeparator);

            writer->WriteBool(NSTokens::Key::isWellKnownToken, snapValue->OptWellKnownToken != TTD_INVALID_WELLKNOWN_TOKEN, NSTokens::Separator::CommaSeparator);
            if(snapValue->OptWellKnownToken != TTD_INVALID_WELLKNOWN_TOKEN)
            {
                writer->WriteWellKnownToken(NSTokens::Key::wellKnownToken, snapValue->OptWellKnownToken, NSTokens::Separator::CommaSeparator);
            }
            else
            {
                switch(snapValue->SnapType->JsTypeId)
                {
                case Js::TypeIds_Undefined:
                case Js::TypeIds_Null:
                    break;
                case Js::TypeIds_Boolean:
                    writer->WriteBool(NSTokens::Key::boolVal, snapValue->u_boolValue ? true : false, NSTokens::Separator::CommaSeparator);
                    break;
                case Js::TypeIds_Number:
                    writer->WriteDouble(NSTokens::Key::doubleVal, snapValue->u_doubleValue, NSTokens::Separator::CommaSeparator);
                    break;
                case Js::TypeIds_Int64Number:
                    writer->WriteInt64(NSTokens::Key::i64Val, snapValue->u_int64Value, NSTokens::Separator::CommaSeparator);
                    break;
                case Js::TypeIds_UInt64Number:
                    writer->WriteUInt64(NSTokens::Key::u64Val, snapValue->u_uint64Value, NSTokens::Separator::CommaSeparator);
                    break;
                case Js::TypeIds_String:
                    writer->WriteString(NSTokens::Key::stringVal, *(snapValue->m_optStringValue), NSTokens::Separator::CommaSeparator);
                    break;
                case Js::TypeIds_Symbol:
                    writer->WriteInt32(NSTokens::Key::propertyId, snapValue->u_propertyIdValue, NSTokens::Separator::CommaSeparator);
                    break;
                default:
                    AssertMsg(false, "These are supposed to be primitive values e.g., no pointers or properties.");
                    break;
                }
            }
            writer->WriteRecordEnd();
        }

        void ParseSnapPrimitiveValue(SnapPrimitiveValue* snapValue, bool readSeperator, FileReader* reader, SlabAllocator& alloc, const TTDIdentifierDictionary<TTD_PTR_ID, NSSnapType::SnapType*>& ptrIdToTypeMap)
        {
            reader->ReadRecordStart(readSeperator);
            snapValue->PrimitiveValueId = reader->ReadAddr(NSTokens::Key::primitiveId);

            TTD_PTR_ID snapTypeId = reader->ReadAddr(NSTokens::Key::typeId, true);
            snapValue->SnapType = ptrIdToTypeMap.LookupKnownItem(snapTypeId);

            snapValue->ValueLogTag = reader->ReadLogTag(NSTokens::Key::logTag, true);

            bool isWellKnown = reader->ReadBool(NSTokens::Key::isWellKnownToken, true);
            if(isWellKnown)
            {
                snapValue->OptWellKnownToken = reader->ReadWellKnownToken(NSTokens::Key::wellKnownToken, alloc, true);

                //just clear it to help avoid any confusion later
                snapValue->u_uint64Value = 0;
            }
            else
            {
                snapValue->OptWellKnownToken = nullptr;

                switch(snapValue->SnapType->JsTypeId)
                {
                case Js::TypeIds_Undefined:
                case Js::TypeIds_Null:
                    break;
                case Js::TypeIds_Boolean:
                    snapValue->u_boolValue = reader->ReadBool(NSTokens::Key::boolVal, true);
                    break;
                case Js::TypeIds_Number:
                    snapValue->u_doubleValue = reader->ReadDouble(NSTokens::Key::doubleVal, true);
                    break;
                case Js::TypeIds_Int64Number:
                    snapValue->u_int64Value = reader->ReadInt64(NSTokens::Key::i64Val, true);
                    break;
                case Js::TypeIds_UInt64Number:
                    snapValue->u_uint64Value = reader->ReadUInt64(NSTokens::Key::u64Val, true);
                    break;
                case Js::TypeIds_String:
                    snapValue->m_optStringValue = alloc.SlabAllocateStruct<TTString>();
                    reader->ReadString(NSTokens::Key::stringVal, alloc, *(snapValue->m_optStringValue), true);
                    break;
                case Js::TypeIds_Symbol:
                    snapValue->u_propertyIdValue = (Js::PropertyId)reader->ReadInt32(NSTokens::Key::propertyId, true);
                    break;
                default:
                    AssertMsg(false, "These are supposed to be primitive values e.g., no pointers or properties.");
                    break;
                }
            }

            reader->ReadRecordEnd();
        }

#if ENABLE_SNAPSHOT_COMPARE 
        void AssertSnapEquiv(const SnapPrimitiveValue* v1, const SnapPrimitiveValue* v2, TTDCompareMap& compareMap)
        {
            compareMap.DiagnosticAssert(v1->SnapType->JsTypeId == v2->SnapType->JsTypeId);
            compareMap.DiagnosticAssert(v1->ValueLogTag == v2->ValueLogTag);

            NSSnapType::AssertSnapEquiv(v1->SnapType, v2->SnapType, compareMap);

            if(v1->OptWellKnownToken != TTD_INVALID_WELLKNOWN_TOKEN || v2->OptWellKnownToken != TTD_INVALID_WELLKNOWN_TOKEN)
            {
                compareMap.DiagnosticAssert(v1->OptWellKnownToken != TTD_INVALID_WELLKNOWN_TOKEN && v2->OptWellKnownToken != TTD_INVALID_WELLKNOWN_TOKEN);
                compareMap.DiagnosticAssert(TTD_DIAGNOSTIC_COMPARE_WELLKNOWN_TOKENS(v1->OptWellKnownToken, v2->OptWellKnownToken));
            }
            else
            {
                switch(v1->SnapType->JsTypeId)
                {
                case Js::TypeIds_Undefined:
                case Js::TypeIds_Null:
                    break;
                case Js::TypeIds_Boolean:
                    compareMap.DiagnosticAssert((v1->u_boolValue ? true : false) == (v2->u_boolValue ? true : false));
                    break;
                case Js::TypeIds_Number:
                    compareMap.DiagnosticAssert(v1->u_doubleValue == v2->u_doubleValue); //This may be problematic wrt. precise FP values
                    break;
                case Js::TypeIds_Int64Number:
                    compareMap.DiagnosticAssert(v1->u_int64Value == v2->u_int64Value);
                    break;
                case Js::TypeIds_UInt64Number:
                    compareMap.DiagnosticAssert(v1->u_uint64Value == v2->u_uint64Value);
                    break;
                case Js::TypeIds_String:
                    compareMap.DiagnosticAssert(TTStringEQForDiagnostics(*(v1->m_optStringValue), *(v2->m_optStringValue)));
                    break;
                case Js::TypeIds_Symbol:
                    compareMap.DiagnosticAssert(v1->u_propertyIdValue == v2->u_propertyIdValue);
                    break;
                default:
                    AssertMsg(false, "These are supposed to be primitive values e.g., no pointers or properties.");
                    break;
                }
            }
        }
#endif

        //////////////////

        Js::Var* InflateSlotArrayInfo(const SlotArrayInfo* slotInfo, InflateMap* inflator)
        {
            Js::ScriptContext* ctx = inflator->LookupScriptContext(slotInfo->ScriptContextTag);
            Js::Var* slotArray = RecyclerNewArray(ctx->GetRecycler(), Js::Var, slotInfo->SlotCount + Js::ScopeSlots::FirstSlotIndex);

            Js::ScopeSlots scopeSlots(slotArray);
            scopeSlots.SetCount(slotInfo->SlotCount);

            if(slotInfo->isFunctionBodyMetaData)
            {
                Js::FunctionBody* fbody = inflator->LookupFunctionBody(slotInfo->OptFunctionBodyId);
                scopeSlots.SetScopeMetadata(fbody);
            }
            else
            {
                //TODO: when we do better with debuggerscope we should set this to a real value
                scopeSlots.SetScopeMetadata(nullptr);
            }

            for(uint32 j = 0; j < slotInfo->SlotCount; j++)
            {
                Js::Var sval = inflator->InflateTTDVar(slotInfo->Slots[j]);
                scopeSlots.Set(j, sval);
            }

            return slotArray;
        }

        void EmitSlotArrayInfo(const SlotArrayInfo* slotInfo, FileWriter* writer, NSTokens::Separator separator)
        {
            writer->WriteRecordStart(separator);
            writer->AdjustIndent(1);

            writer->WriteAddr(NSTokens::Key::slotId, slotInfo->SlotId, NSTokens::Separator::BigSpaceSeparator);
            writer->WriteAddr(NSTokens::Key::ctxTag, slotInfo->ScriptContextTag, NSTokens::Separator::CommaSeparator);

            writer->WriteBool(NSTokens::Key::isFunctionMetaData, slotInfo->isFunctionBodyMetaData, NSTokens::Separator::CommaSeparator);
            if(slotInfo->isFunctionBodyMetaData)
            {
                writer->WriteAddr(NSTokens::Key::functionBodyId, slotInfo->OptFunctionBodyId, NSTokens::Separator::CommaSeparator);
            }
            else
            {
                //TODO: emit debugger scope metadata
            }

            writer->WriteLengthValue(slotInfo->SlotCount, NSTokens::Separator::CommaAndBigSpaceSeparator);
            writer->WriteSequenceStart_DefaultKey(NSTokens::Separator::CommaAndBigSpaceSeparator);
            writer->AdjustIndent(1);
            for(uint32 i = 0; i < slotInfo->SlotCount; ++i)
            {
                NSTokens::Separator sep = (i != 0 ? NSTokens::Separator::CommaAndBigSpaceSeparator : NSTokens::Separator::BigSpaceSeparator);

#if ENABLE_TTD_INTERNAL_DIAGNOSTICS
                writer->WriteRecordStart(sep);
                writer->WriteUInt32(NSTokens::Key::pid, slotInfo->DebugPIDArray[i], NSTokens::Separator::NoSeparator);
                writer->WriteKey(NSTokens::Key::entry, NSTokens::Separator::CommaSeparator);

                sep = NSTokens::Separator::NoSeparator;
#endif

                EmitTTDVar(slotInfo->Slots[i], writer, sep);

#if ENABLE_TTD_INTERNAL_DIAGNOSTICS
                writer->WriteRecordEnd();
#endif
            }
            writer->AdjustIndent(-1);
            writer->WriteSequenceEnd(NSTokens::Separator::BigSpaceSeparator);

            writer->AdjustIndent(-1);
            writer->WriteRecordEnd(NSTokens::Separator::BigSpaceSeparator);
        }

        void ParseSlotArrayInfo(SlotArrayInfo* slotInfo, bool readSeperator, FileReader* reader, SlabAllocator& alloc)
        {
            reader->ReadRecordStart(readSeperator);

            slotInfo->SlotId = reader->ReadAddr(NSTokens::Key::slotId);
            slotInfo->ScriptContextTag = reader->ReadAddr(NSTokens::Key::ctxTag, true);

            slotInfo->isFunctionBodyMetaData = reader->ReadBool(NSTokens::Key::isFunctionMetaData, true);
            slotInfo->OptFunctionBodyId = TTD_INVALID_PTR_ID;
            if(slotInfo->isFunctionBodyMetaData)
            {
                slotInfo->OptFunctionBodyId = reader->ReadAddr(NSTokens::Key::functionBodyId, true);
            }
            else
            {
                //TODO: emit debugger scope metadata
            }

            slotInfo->SlotCount = reader->ReadLengthValue(true);
            reader->ReadSequenceStart_WDefaultKey(true);

            slotInfo->Slots = alloc.SlabAllocateArray<TTDVar>(slotInfo->SlotCount);

#if ENABLE_TTD_INTERNAL_DIAGNOSTICS
            slotInfo->DebugPIDArray = alloc.SlabAllocateArray<Js::PropertyId>(slotInfo->SlotCount);
#endif

            for(uint32 i = 0; i < slotInfo->SlotCount; ++i)
            {
                bool readSeparator = (i != 0);

#if ENABLE_TTD_INTERNAL_DIAGNOSTICS
                reader->ReadRecordStart(readSeparator);
                slotInfo->DebugPIDArray[i] = (Js::PropertyId)reader->ReadUInt32(NSTokens::Key::pid);
                reader->ReadKey(NSTokens::Key::entry, true);

                readSeparator = false;
#endif

                slotInfo->Slots[i] = ParseTTDVar(readSeparator, reader);

#if ENABLE_TTD_INTERNAL_DIAGNOSTICS
                reader->ReadRecordEnd();
#endif
            }
            reader->ReadSequenceEnd();

            reader->ReadRecordEnd();
        }

#if ENABLE_SNAPSHOT_COMPARE 
        void AssertSnapEquiv(const SlotArrayInfo* sai1, const SlotArrayInfo* sai2, TTDCompareMap& compareMap)
        {
            compareMap.DiagnosticAssert(sai1->ScriptContextTag == sai2->ScriptContextTag);

            compareMap.DiagnosticAssert(sai1->isFunctionBodyMetaData == sai2->isFunctionBodyMetaData);
            if(sai1->isFunctionBodyMetaData)
            {
                compareMap.CheckConsistentAndAddPtrIdMapping_FunctionBody(sai1->OptFunctionBodyId, sai2->OptFunctionBodyId);
            }
            else
            {
                //TODO: emit debugger scope metadata
            }

            compareMap.DiagnosticAssert(sai1->SlotCount == sai2->SlotCount);
            for(uint32 i = 0; i < sai1->SlotCount; ++i)
            {
#if ENABLE_TTD_INTERNAL_DIAGNOSTICS
                compareMap.DiagnosticAssert(sai1->DebugPIDArray[i] == sai2->DebugPIDArray[i]);
#endif

                AssertSnapEquivTTDVar_SlotArray(sai1->Slots[i], sai2->Slots[i], compareMap, i);
            }
        }
#endif

        Js::FrameDisplay* InflateScriptFunctionScopeInfo(const ScriptFunctionScopeInfo* funcScopeInfo, InflateMap* inflator)
        {
            Js::ScriptContext* ctx = inflator->LookupScriptContext(funcScopeInfo->ScriptContextTag);

            Js::FrameDisplay* environment = RecyclerNewPlus(ctx->GetRecycler(), funcScopeInfo->ScopeCount * sizeof(Js::Var), Js::FrameDisplay, funcScopeInfo->ScopeCount);

            for(uint16 i = 0; i < funcScopeInfo->ScopeCount; ++i)
            {
                const ScopeInfoEntry& scp = funcScopeInfo->ScopeArray[i];

                switch(scp.Tag)
                {
                case Js::ScopeType::ScopeType_ActivationObject:
                case Js::ScopeType::ScopeType_WithScope:
                {
                    Js::Var sval = inflator->LookupObject(scp.IDValue);
                    environment->SetItem(i, sval);
                    break;
                }
                case Js::ScopeType::ScopeType_SlotArray:
                {
                    Js::Var* saval = inflator->LookupSlotArray(scp.IDValue);
                    environment->SetItem(i, saval);
                    break;
                }
                default:
                    AssertMsg(false, "Unknown scope kind");
                    break;
                }
            }

            return environment;
        }

        void EmitScriptFunctionScopeInfo(const ScriptFunctionScopeInfo* funcScopeInfo, FileWriter* writer, NSTokens::Separator separator)
        {
            writer->WriteRecordStart(separator);

            writer->WriteAddr(NSTokens::Key::scopeId, funcScopeInfo->ScopeId);
            writer->WriteAddr(NSTokens::Key::ctxTag, funcScopeInfo->ScriptContextTag, NSTokens::Separator::CommaSeparator);
            writer->WriteUInt32(NSTokens::Key::count, funcScopeInfo->ScopeCount, NSTokens::Separator::CommaSeparator);

            writer->WriteSequenceStart_DefaultKey(NSTokens::Separator::CommaSeparator);

            for(uint32 i = 0; i < funcScopeInfo->ScopeCount; ++i)
            {
                writer->WriteRecordStart(i != 0 ? NSTokens::Separator::CommaSeparator : NSTokens::Separator::NoSeparator);

                writer->WriteTag<Js::ScopeType>(NSTokens::Key::scopeType, funcScopeInfo->ScopeArray[i].Tag);
                writer->WriteAddr(NSTokens::Key::subscopeId, funcScopeInfo->ScopeArray[i].IDValue, NSTokens::Separator::CommaSeparator);

                writer->WriteRecordEnd();
            }

            writer->WriteSequenceEnd();

            writer->WriteRecordEnd();
        }

        void ParseScriptFunctionScopeInfo(ScriptFunctionScopeInfo* funcScopeInfo, bool readSeperator, FileReader* reader, SlabAllocator& alloc)
        {
            reader->ReadRecordStart(readSeperator);

            funcScopeInfo->ScopeId = reader->ReadAddr(NSTokens::Key::scopeId);
            funcScopeInfo->ScriptContextTag = reader->ReadAddr(NSTokens::Key::ctxTag, true);
            funcScopeInfo->ScopeCount = (uint16)reader->ReadUInt32(NSTokens::Key::count, true);

            reader->ReadSequenceStart_WDefaultKey(true);

            funcScopeInfo->ScopeArray = alloc.SlabAllocateArray<ScopeInfoEntry>(funcScopeInfo->ScopeCount);
            for(uint32 i = 0; i < funcScopeInfo->ScopeCount; ++i)
            {
                reader->ReadRecordStart(i != 0);

                funcScopeInfo->ScopeArray[i].Tag = reader->ReadTag<Js::ScopeType>(NSTokens::Key::scopeType);
                funcScopeInfo->ScopeArray[i].IDValue = reader->ReadAddr(NSTokens::Key::subscopeId, true);

                reader->ReadRecordEnd();
            }

            reader->ReadSequenceEnd();

            reader->ReadRecordEnd();
        }

#if ENABLE_SNAPSHOT_COMPARE 
        void AssertSnapEquiv(const ScriptFunctionScopeInfo* funcScopeInfo1, const ScriptFunctionScopeInfo* funcScopeInfo2, TTDCompareMap& compareMap)
        {
            compareMap.DiagnosticAssert(funcScopeInfo1->ScriptContextTag == funcScopeInfo2->ScriptContextTag);

            compareMap.DiagnosticAssert(funcScopeInfo1->ScopeCount == funcScopeInfo2->ScopeCount);
            for(uint32 i = 0; i < funcScopeInfo1->ScopeCount; ++i)
            {
                compareMap.DiagnosticAssert(funcScopeInfo1->ScopeArray[i].Tag == funcScopeInfo2->ScopeArray[i].Tag);
                compareMap.CheckConsistentAndAddPtrIdMapping_Scope(funcScopeInfo1->ScopeArray[i].IDValue, funcScopeInfo2->ScopeArray[i].IDValue, i);
            }
        }
#endif

        //////////////////

        Js::JavascriptPromiseCapability* InflatePromiseCapabilityInfo(const SnapPromiseCapabilityInfo* capabilityInfo, Js::ScriptContext* ctx, InflateMap* inflator)
        {
            if(!inflator->IsPromiseInfoDefined<Js::JavascriptPromiseCapability>(capabilityInfo->CapabilityId))
            {
                Js::Var promise = inflator->InflateTTDVar(capabilityInfo->PromiseVar);
                Js::RecyclableObject* resolve = inflator->LookupObject(capabilityInfo->ResolveObjId);
                Js::RecyclableObject* reject = inflator->LookupObject(capabilityInfo->RejectObjId);

                Js::JavascriptPromiseCapability* res = ctx->GetLibrary()->CreatePromiseCapability_TTD(promise, resolve, reject);
                inflator->AddInflatedPromiseInfo<Js::JavascriptPromiseCapability>(capabilityInfo->CapabilityId, res);
            }

            return inflator->LookupInflatedPromiseInfo<Js::JavascriptPromiseCapability>(capabilityInfo->CapabilityId);
        }

        void EmitPromiseCapabilityInfo(const SnapPromiseCapabilityInfo* capabilityInfo, FileWriter* writer, NSTokens::Separator separator)
        {
            writer->WriteRecordStart(separator);

            writer->WriteAddr(NSTokens::Key::ptrIdVal, capabilityInfo->CapabilityId);

            writer->WriteKey(NSTokens::Key::entry, NSTokens::Separator::CommaSeparator);
            EmitTTDVar(capabilityInfo->PromiseVar, writer, NSTokens::Separator::NoSeparator);

            writer->WriteAddr(NSTokens::Key::ptrIdVal, capabilityInfo->ResolveObjId, NSTokens::Separator::CommaSeparator);
            writer->WriteAddr(NSTokens::Key::ptrIdVal, capabilityInfo->RejectObjId, NSTokens::Separator::CommaSeparator);

            writer->WriteRecordEnd();
        }

        void ParsePromiseCapabilityInfo(SnapPromiseCapabilityInfo* capabilityInfo, bool readSeperator, FileReader* reader, SlabAllocator& alloc)
        {
            reader->ReadRecordStart(readSeperator);

            capabilityInfo->CapabilityId = reader->ReadAddr(NSTokens::Key::ptrIdVal);

            reader->ReadKey(NSTokens::Key::entry, true);
            capabilityInfo->PromiseVar = ParseTTDVar(false, reader);

            capabilityInfo->ResolveObjId = reader->ReadAddr(NSTokens::Key::ptrIdVal, true);
            capabilityInfo->RejectObjId = reader->ReadAddr(NSTokens::Key::ptrIdVal, true);

            reader->ReadRecordEnd();
        }


#if ENABLE_SNAPSHOT_COMPARE 
        void AssertSnapEquiv(const SnapPromiseCapabilityInfo* capabilityInfo1, const SnapPromiseCapabilityInfo* capabilityInfo2, TTDCompareMap& compareMap)
        {
            compareMap.CheckConsistentAndAddPtrIdMapping_NoEnqueue(capabilityInfo1->CapabilityId, capabilityInfo2->CapabilityId);

            AssertSnapEquivTTDVar_Special(capabilityInfo1->PromiseVar, capabilityInfo2->PromiseVar, compareMap, L"promiseVar");
            compareMap.CheckConsistentAndAddPtrIdMapping_Special(capabilityInfo1->ResolveObjId, capabilityInfo2->ResolveObjId, L"resolveObjId");
            compareMap.CheckConsistentAndAddPtrIdMapping_Special(capabilityInfo1->RejectObjId, capabilityInfo2->RejectObjId, L"rejectObjId");
        }
#endif

        Js::JavascriptPromiseReaction* InflatePromiseReactionInfo(const SnapPromiseReactionInfo* reactionInfo, Js::ScriptContext* ctx, InflateMap* inflator)
        {
            if(!inflator->IsPromiseInfoDefined<Js::JavascriptPromiseReaction>(reactionInfo->PromiseReactionId))
            {
                Js::RecyclableObject* handler = inflator->LookupObject(reactionInfo->HandlerObjId);
                Js::JavascriptPromiseCapability* capabilities = InflatePromiseCapabilityInfo(&reactionInfo->Capabilities, ctx, inflator);

                Js::JavascriptPromiseReaction* res = ctx->GetLibrary()->CreatePromiseReaction_TTD(handler, capabilities);
                inflator->AddInflatedPromiseInfo<Js::JavascriptPromiseReaction>(reactionInfo->PromiseReactionId, res);
            }

            return inflator->LookupInflatedPromiseInfo<Js::JavascriptPromiseReaction>(reactionInfo->PromiseReactionId);
        }

        void EmitPromiseReactionInfo(const SnapPromiseReactionInfo* reactionInfo, FileWriter* writer, NSTokens::Separator separator)
        {
            writer->WriteRecordStart(separator);

            writer->WriteAddr(NSTokens::Key::ptrIdVal, reactionInfo->PromiseReactionId);

            writer->WriteAddr(NSTokens::Key::ptrIdVal, reactionInfo->HandlerObjId, NSTokens::Separator::CommaSeparator);
            writer->WriteKey(NSTokens::Key::entry, NSTokens::Separator::CommaSeparator);
            EmitPromiseCapabilityInfo(&reactionInfo->Capabilities, writer, NSTokens::Separator::NoSeparator);

            writer->WriteRecordEnd();
        }

        void ParsePromiseReactionInfo(SnapPromiseReactionInfo* reactionInfo, bool readSeperator, FileReader* reader, SlabAllocator& alloc)
        {
            reader->ReadRecordStart(readSeperator);

            reactionInfo->PromiseReactionId = reader->ReadAddr(NSTokens::Key::ptrIdVal);

            reactionInfo->HandlerObjId = reader->ReadAddr(NSTokens::Key::ptrIdVal, true);
            reader->ReadKey(NSTokens::Key::entry, true);
            ParsePromiseCapabilityInfo(&reactionInfo->Capabilities, false, reader, alloc);

            reader->ReadRecordEnd();
        }

#if ENABLE_SNAPSHOT_COMPARE 
        void AssertSnapEquiv(const SnapPromiseReactionInfo* reactionInfo1, const SnapPromiseReactionInfo* reactionInfo2, TTDCompareMap& compareMap)
        {
            compareMap.CheckConsistentAndAddPtrIdMapping_NoEnqueue(reactionInfo1->PromiseReactionId, reactionInfo2->PromiseReactionId);

            compareMap.CheckConsistentAndAddPtrIdMapping_Special(reactionInfo1->HandlerObjId, reactionInfo2->HandlerObjId, L"handlerObjId");
            AssertSnapEquiv(&(reactionInfo1->Capabilities), &(reactionInfo2->Capabilities), compareMap);
        }
#endif

        //////////////////

        void ExtractTopLevelCommonBodyResolveInfo_InScriptContext(TopLevelCommonBodyResolveInfo* fbInfo, Js::FunctionBody* fb, Js::ModuleID moduleId, DWORD_PTR documentID, LPCWSTR source, uint32 sourceLen)
        {
            fbInfo->FunctionBodyId = TTD_CONVERT_VAR_TO_PTR_ID(fb);
            fbInfo->ScriptContextTag = TTD_EXTRACT_CTX_LOG_TAG(fb->GetScriptContext());

            JsSupport::CopyStringToHeapAllocator(fb->GetDisplayName(), fbInfo->FunctionName);

            fbInfo->ModuleId = moduleId;
            fbInfo->DocumentID = documentID;
            JsSupport::CopyStringToHeapAllocator(fb->GetSourceContextInfo()->url, fbInfo->SourceUri);

            JsSupport::CopyStringToHeapAllocatorWLength(source, sourceLen, fbInfo->SourceCode);
        }

        void UnloadTopLevelCommonBodyResolveInfo(TopLevelCommonBodyResolveInfo* fbInfo)
        {
            JsSupport::DeleteStringFromHeapAllocator(fbInfo->FunctionName);

            JsSupport::DeleteStringFromHeapAllocator(fbInfo->SourceUri);
            JsSupport::DeleteStringFromHeapAllocator(fbInfo->SourceCode);
        }

        void ExtractTopLevelCommonBodyResolveInfo_InShapshot(TopLevelCommonBodyResolveInfo* fbInfoDest, const TopLevelCommonBodyResolveInfo* fbInfoSrc, SlabAllocator& alloc)
        {
            fbInfoDest->FunctionBodyId = fbInfoSrc->FunctionBodyId;
            fbInfoDest->ScriptContextTag = fbInfoSrc->ScriptContextTag;

            alloc.CopyStringIntoWLength(fbInfoSrc->FunctionName.Contents, fbInfoSrc->FunctionName.Length, fbInfoDest->FunctionName);

            fbInfoDest->ModuleId = fbInfoSrc->ModuleId;
            fbInfoDest->DocumentID = fbInfoSrc->DocumentID;

            InitializeAsNullPtrTTString(fbInfoDest->SourceUri);
            if(!IsNullPtrTTString(fbInfoSrc->SourceUri))
            {
                alloc.CopyStringIntoWLength(fbInfoSrc->SourceUri.Contents, fbInfoSrc->SourceUri.Length, fbInfoDest->SourceUri);
            }

            alloc.CopyStringIntoWLength(fbInfoSrc->SourceCode.Contents, fbInfoSrc->SourceCode.Length, fbInfoDest->SourceCode);
        }

        void EmitTopLevelCommonBodyResolveInfo(const TopLevelCommonBodyResolveInfo* fbInfo, bool emitInline, LPCWSTR sourceDir, IOStreamFunctions& streamFunctions, FileWriter* writer, NSTokens::Separator separator)
        {
            writer->WriteRecordStart(separator);
            writer->WriteAddr(NSTokens::Key::functionBodyId, fbInfo->FunctionBodyId);
            writer->WriteAddr(NSTokens::Key::ctxTag, fbInfo->ScriptContextTag, NSTokens::Separator::CommaSeparator);

            writer->WriteString(NSTokens::Key::name, fbInfo->FunctionName, NSTokens::Separator::CommaSeparator);

            writer->WriteUInt64(NSTokens::Key::moduleId, fbInfo->ModuleId, NSTokens::Separator::CommaSeparator);
            writer->WriteUInt64(NSTokens::Key::documentId, fbInfo->DocumentID, NSTokens::Separator::CommaSeparator);
            writer->WriteString(NSTokens::Key::uri, fbInfo->SourceUri, NSTokens::Separator::CommaSeparator);

            if(emitInline || IsNullPtrTTString(fbInfo->SourceUri))
            {
                writer->WriteString(NSTokens::Key::src, fbInfo->SourceCode, NSTokens::Separator::CommaSeparator);
            }
            else
            {
                writer->WriteLengthValue(fbInfo->SourceCode.Length, NSTokens::Separator::CommaSeparator);

                LPCWSTR docId = writer->FormatNumber(fbInfo->DocumentID);
                JsSupport::WriteCodeToFile(streamFunctions, sourceDir, docId, fbInfo->SourceUri.Contents, fbInfo->SourceCode.Contents, fbInfo->SourceCode.Length);
            }
        }

        void ParseTopLevelCommonBodyResolveInfo(TopLevelCommonBodyResolveInfo* fbInfo, bool readSeperator, bool parseInline, LPCWSTR sourceDir, IOStreamFunctions& streamFunctions, FileReader* reader, SlabAllocator& alloc)
        {
            reader->ReadRecordStart(readSeperator);
            fbInfo->FunctionBodyId = reader->ReadAddr(NSTokens::Key::functionBodyId);
            fbInfo->ScriptContextTag = reader->ReadAddr(NSTokens::Key::ctxTag, true);

            reader->ReadString(NSTokens::Key::name, alloc, fbInfo->FunctionName, true);

            fbInfo->ModuleId = (Js::ModuleID)reader->ReadUInt64(NSTokens::Key::moduleId, true);
            fbInfo->DocumentID = (DWORD_PTR)reader->ReadUInt64(NSTokens::Key::documentId, true);
            reader->ReadString(NSTokens::Key::uri, alloc, fbInfo->SourceUri, true);

            if(parseInline || IsNullPtrTTString(fbInfo->SourceUri))
            {
                reader->ReadString(NSTokens::Key::src, alloc, fbInfo->SourceCode, true);
            }
            else
            {
                fbInfo->SourceCode.Length = reader->ReadLengthValue(true);
                fbInfo->SourceCode.Contents = alloc.SlabAllocateArray<wchar>(fbInfo->SourceCode.Length + 1);

                LPCWSTR docId = reader->FormatNumber(fbInfo->DocumentID);
                JsSupport::ReadCodeFromFile(streamFunctions, sourceDir, docId, fbInfo->SourceUri.Contents, fbInfo->SourceCode.Contents, fbInfo->SourceCode.Length);
            }
        }

#if ENABLE_SNAPSHOT_COMPARE
        void AssertSnapEquiv(const TopLevelCommonBodyResolveInfo* fbInfo1, const TopLevelCommonBodyResolveInfo* fbInfo2, TTDCompareMap& compareMap)
        {
            compareMap.DiagnosticAssert(fbInfo1->ScriptContextTag == fbInfo2->ScriptContextTag);
            compareMap.DiagnosticAssert(TTStringEQForDiagnostics(fbInfo1->FunctionName, fbInfo2->FunctionName));

            //
            //TODO: we don't force documentids to be the same accross re-inflate yet but when we do we should check this as well
            //
            //TTD_DIAGNOSTIC_ASSERT(fbInfo1->DocumentID, fbInfo2->DocumentID);

            compareMap.DiagnosticAssert(fbInfo1->ModuleId == fbInfo2->ModuleId);

            compareMap.DiagnosticAssert(TTStringEQForDiagnostics(fbInfo1->SourceUri, fbInfo2->SourceUri));
            compareMap.DiagnosticAssert(TTStringEQForDiagnostics(fbInfo1->SourceCode, fbInfo2->SourceCode));
        }
#endif

        ////
        //Regular script-load functions

        void ExtractTopLevelLoadedFunctionBodyInfo_InScriptContext(TopLevelScriptLoadFunctionBodyResolveInfo* fbInfo, Js::FunctionBody* fb, Js::ModuleID moduleId, DWORD_PTR documentID, LPCWSTR source, uint32 sourceLen, LoadScriptFlag loadFlag)
        {
            NSSnapValues::ExtractTopLevelCommonBodyResolveInfo_InScriptContext(&fbInfo->TopLevelBase, fb, moduleId, documentID, source, sourceLen);

            fbInfo->LoadFlag = loadFlag;
        }

        void UnloadTopLevelLoadedFunctionBodyInfo(TopLevelScriptLoadFunctionBodyResolveInfo* fbInfo)
        {
            NSSnapValues::UnloadTopLevelCommonBodyResolveInfo(&fbInfo->TopLevelBase);
        }

        void ExtractTopLevelLoadedFunctionBodyInfo_InShapshot(TopLevelScriptLoadFunctionBodyResolveInfo* fbInfoDest, const TopLevelScriptLoadFunctionBodyResolveInfo* fbInfoSrc, SlabAllocator& alloc)
        {
            NSSnapValues::ExtractTopLevelCommonBodyResolveInfo_InShapshot(&fbInfoDest->TopLevelBase, &fbInfoSrc->TopLevelBase, alloc);

            fbInfoDest->LoadFlag = fbInfoSrc->LoadFlag;
        }

        Js::FunctionBody* InflateTopLevelLoadedFunctionBodyInfo(const TopLevelScriptLoadFunctionBodyResolveInfo* fbInfo, Js::ScriptContext* ctx)
        {
            LPCWSTR script = fbInfo->TopLevelBase.SourceCode.Contents;
            uint32 scriptLength = fbInfo->TopLevelBase.SourceCode.Length;
            DWORD_PTR sourceContext = fbInfo->TopLevelBase.DocumentID;

            AssertMsg(ctx->GetSourceContextInfo(sourceContext, nullptr) == nullptr, "On inflate we should either have clean ctxts or we want to optimize the inflate process by skipping redoing this work!!!");

            SourceContextInfo * sourceContextInfo = ctx->CreateSourceContextInfo(sourceContext, fbInfo->TopLevelBase.SourceUri.Contents, fbInfo->TopLevelBase.SourceUri.Length, nullptr);
            SRCINFO si = {
                /* sourceContextInfo   */ sourceContextInfo,
                /* dlnHost             */ 0,
                /* ulColumnHost        */ 0,
                /* lnMinHost           */ 0,
                /* ichMinHost          */ 0,
                /* ichLimHost          */ static_cast<ULONG>(scriptLength), // OK to truncate since this is used to limit sourceText in debugDocument/compilation errors.
                /* ulCharOffset        */ 0,
                /* mod                 */ fbInfo->TopLevelBase.ModuleId,
                /* grfsi               */ 0
            };

            Js::Utf8SourceInfo* utf8SourceInfo;
            CompileScriptException se;
            Js::JavascriptFunction* scriptFunction = nullptr;
            BEGIN_LEAVE_SCRIPT_WITH_EXCEPTION(ctx)
            {
                scriptFunction = ctx->LoadScript((const byte*)script, scriptLength * sizeof(wchar_t), &si, &se, &utf8SourceInfo, Js::Constants::GlobalCode, fbInfo->LoadFlag);
            }
            END_LEAVE_SCRIPT_WITH_EXCEPTION(ctx);
            AssertMsg(scriptFunction != nullptr, "Something went wrong");

            Js::FunctionBody* globalBody = TTD::JsSupport::ForceAndGetFunctionBody(scriptFunction->GetParseableFunctionInfo());

            ////
            //We don't do this automatically in the eval helper so do it here
            TTD::NSSnapValues::TopLevelScriptLoadFunctionBodyResolveInfo* tbfi = HeapNewStruct(TTD::NSSnapValues::TopLevelScriptLoadFunctionBodyResolveInfo);
            TTD::NSSnapValues::ExtractTopLevelLoadedFunctionBodyInfo_InScriptContext(tbfi, globalBody, fbInfo->TopLevelBase.ModuleId, fbInfo->TopLevelBase.DocumentID, fbInfo->TopLevelBase.SourceCode.Contents, fbInfo->TopLevelBase.SourceCode.Length, fbInfo->LoadFlag);
            ctx->m_ttdTopLevelScriptLoad.Add(tbfi);

            //walk global body to (1) add functions to pin set (2) build parent map
            ctx->ProcessFunctionBodyOnLoad(globalBody, nullptr);

            const HostScriptContextCallbackFunctor& hostFunctor = ctx->GetCallbackFunctor_TTD();
            if(hostFunctor.pfOnScriptLoadCallback != nullptr)
            {
                hostFunctor.pfOnScriptLoadCallback(hostFunctor.HostData, scriptFunction, utf8SourceInfo, &se);
            }
            ////

            return globalBody;
        }

        void EmitTopLevelLoadedFunctionBodyInfo(const TopLevelScriptLoadFunctionBodyResolveInfo* fbInfo, LPCWSTR sourceDir, IOStreamFunctions& streamFunctions, FileWriter* writer, NSTokens::Separator separator)
        {
            NSSnapValues::EmitTopLevelCommonBodyResolveInfo(&fbInfo->TopLevelBase, false, sourceDir, streamFunctions, writer, separator);

            writer->WriteTag<LoadScriptFlag>(NSTokens::Key::loadFlag, fbInfo->LoadFlag, NSTokens::Separator::CommaSeparator);

            writer->WriteRecordEnd();
        }

        void ParseTopLevelLoadedFunctionBodyInfo(TopLevelScriptLoadFunctionBodyResolveInfo* fbInfo, bool readSeperator, LPCWSTR sourceDir, IOStreamFunctions& streamFunctions, FileReader* reader, SlabAllocator& alloc)
        {
            NSSnapValues::ParseTopLevelCommonBodyResolveInfo(&fbInfo->TopLevelBase, readSeperator, false, sourceDir, streamFunctions, reader, alloc);

            fbInfo->LoadFlag = reader->ReadTag<LoadScriptFlag>(NSTokens::Key::loadFlag, true);

            reader->ReadRecordEnd();
        }

#if ENABLE_SNAPSHOT_COMPARE 
        void AssertSnapEquiv(const TopLevelScriptLoadFunctionBodyResolveInfo* fbInfo1, const TopLevelScriptLoadFunctionBodyResolveInfo* fbInfo2, TTDCompareMap& compareMap)
        {
            compareMap.DiagnosticAssert(fbInfo1->LoadFlag == fbInfo2->LoadFlag);

            AssertSnapEquiv(&(fbInfo1->TopLevelBase), &(fbInfo2->TopLevelBase), compareMap);
        }
#endif

        ////
        //'new Function(...)' functions

        void ExtractTopLevelNewFunctionBodyInfo_InScriptContext(TopLevelNewFunctionBodyResolveInfo* fbInfo, Js::FunctionBody* fb, Js::ModuleID moduleId, LPCWSTR source, uint32 sourceLen)
        {
            NSSnapValues::ExtractTopLevelCommonBodyResolveInfo_InScriptContext(&fbInfo->TopLevelBase, fb, moduleId, 0, source, sourceLen);
        }

        void UnloadTopLevelNewFunctionBodyInfo(TopLevelNewFunctionBodyResolveInfo* fbInfo)
        {
            NSSnapValues::UnloadTopLevelCommonBodyResolveInfo(&fbInfo->TopLevelBase);
        }

        void ExtractTopLevelNewFunctionBodyInfo_InShapshot(TopLevelNewFunctionBodyResolveInfo* fbInfoDest, const TopLevelNewFunctionBodyResolveInfo* fbInfoSrc, SlabAllocator& alloc)
        {
            NSSnapValues::ExtractTopLevelCommonBodyResolveInfo_InShapshot(&fbInfoDest->TopLevelBase, &fbInfoSrc->TopLevelBase, alloc);
        }

        Js::FunctionBody* InflateTopLevelNewFunctionBodyInfo(const TopLevelNewFunctionBodyResolveInfo* fbInfo, Js::ScriptContext* ctx)
        {
            // Bug 1105479. Get the module id from the caller
            Js::ModuleID moduleID = kmodGlobal;
            BOOL strictMode = FALSE;

            Js::JavascriptFunction* pfuncScript = ctx->GetGlobalObject()->EvalHelper(ctx, fbInfo->TopLevelBase.SourceCode.Contents, (int32)fbInfo->TopLevelBase.SourceCode.Length, moduleID, fscrNil, Js::Constants::FunctionCode, TRUE, TRUE, strictMode);
            AssertMsg(pfuncScript != nullptr, "Something went wrong!!!");

            // Indicate that this is a top-level function. We don't pass the fscrGlobalCode flag to the eval helper,
            // or it will return the global function that wraps the declared function body, as though it were an eval.
            // But we want, for instance, to be able to verify that we did the right amount of deferred parsing.
            Js::ParseableFunctionInfo* functionInfo = pfuncScript->GetParseableFunctionInfo();
            functionInfo->SetGrfscr(functionInfo->GetGrfscr() | fscrGlobalCode);

            Js::EvalMapString key(fbInfo->TopLevelBase.SourceCode.Contents, (int32)fbInfo->TopLevelBase.SourceCode.Length, moduleID, strictMode, /* isLibraryCode = */ false);
            ctx->AddToNewFunctionMap(key, functionInfo);

            Js::FunctionBody* fb = JsSupport::ForceAndGetFunctionBody(pfuncScript->GetParseableFunctionInfo());

            ////
            //We don't do this automatically in the eval helper so do it here
            TTD::NSSnapValues::TopLevelNewFunctionBodyResolveInfo* tbfi = HeapNewStruct(TTD::NSSnapValues::TopLevelNewFunctionBodyResolveInfo);
            TTD::NSSnapValues::ExtractTopLevelNewFunctionBodyInfo_InScriptContext(tbfi, fb, moduleID, fbInfo->TopLevelBase.SourceCode.Contents, fbInfo->TopLevelBase.SourceCode.Length);
            ctx->m_ttdTopLevelNewFunction.Add(tbfi);

            //walk global body to (1) add functions to pin set (2) build parent map
            ctx->ProcessFunctionBodyOnLoad(fb, nullptr);
            ////

            return fb;
        }

        void EmitTopLevelNewFunctionBodyInfo(const TopLevelNewFunctionBodyResolveInfo* fbInfo, LPCWSTR sourceDir, IOStreamFunctions& streamFunctions, FileWriter* writer, NSTokens::Separator separator)
        {
            NSSnapValues::EmitTopLevelCommonBodyResolveInfo(&fbInfo->TopLevelBase, true, sourceDir, streamFunctions, writer, separator);

            writer->WriteRecordEnd();
        }

        void ParseTopLevelNewFunctionBodyInfo(TopLevelNewFunctionBodyResolveInfo* fbInfo, bool readSeperator, LPCWSTR sourceDir, IOStreamFunctions& streamFunctions, FileReader* reader, SlabAllocator& alloc)
        {
            NSSnapValues::ParseTopLevelCommonBodyResolveInfo(&fbInfo->TopLevelBase, readSeperator, false, sourceDir, streamFunctions, reader, alloc);

            reader->ReadRecordEnd();
        }

#if ENABLE_SNAPSHOT_COMPARE 
        void AssertSnapEquiv(const TopLevelNewFunctionBodyResolveInfo* fbInfo1, const TopLevelNewFunctionBodyResolveInfo* fbInfo2, TTDCompareMap& compareMap)
        {
            AssertSnapEquiv(&(fbInfo1->TopLevelBase), &(fbInfo2->TopLevelBase), compareMap);
        }
#endif

        ////
        //'eval(...)' functions

        void ExtractTopLevelEvalFunctionBodyInfo_InScriptContext(TopLevelEvalFunctionBodyResolveInfo* fbInfo, Js::FunctionBody* fb, Js::ModuleID moduleId, LPCWSTR source, uint32 sourceLen, ulong grfscr, bool registerDocument, BOOL isIndirect, BOOL strictMode)
        {
            NSSnapValues::ExtractTopLevelCommonBodyResolveInfo_InScriptContext(&fbInfo->TopLevelBase, fb, moduleId, 0, source, sourceLen);

            fbInfo->EvalFlags = grfscr;
            fbInfo->RegisterDocument = registerDocument;
            fbInfo->IsIndirect = isIndirect ? true : false;
            fbInfo->IsStrictMode = strictMode ? true : false;
        }

        void UnloadTopLevelEvalFunctionBodyInfo(TopLevelEvalFunctionBodyResolveInfo* fbInfo)
        {
            NSSnapValues::UnloadTopLevelCommonBodyResolveInfo(&fbInfo->TopLevelBase);
        }

        void ExtractTopLevelEvalFunctionBodyInfo_InShapshot(TopLevelEvalFunctionBodyResolveInfo* fbInfoDest, const TopLevelEvalFunctionBodyResolveInfo* fbInfoSrc, SlabAllocator& alloc)
        {
            NSSnapValues::ExtractTopLevelCommonBodyResolveInfo_InShapshot(&fbInfoDest->TopLevelBase, &fbInfoSrc->TopLevelBase, alloc);

            fbInfoDest->EvalFlags = fbInfoSrc->EvalFlags;
            fbInfoDest->RegisterDocument = fbInfoSrc->RegisterDocument;
            fbInfoDest->IsIndirect = fbInfoSrc->IsIndirect;
            fbInfoDest->IsStrictMode = fbInfoSrc->IsStrictMode;
        }

        Js::FunctionBody* InflateTopLevelEvalFunctionBodyInfo(const TopLevelEvalFunctionBodyResolveInfo* fbInfo, Js::ScriptContext* ctx)
        {
            ulong grfscr = ((ulong)fbInfo->EvalFlags) | fscrReturnExpression | fscrEval | fscrEvalCode | fscrGlobalCode;

            LPCWSTR source = fbInfo->TopLevelBase.SourceCode.Contents;
            int32 sourceLen = (int32)fbInfo->TopLevelBase.SourceCode.Length;
            Js::ScriptFunction* pfuncScript = ctx->GetLibrary()->GetGlobalObject()->EvalHelper(ctx, source, sourceLen, fbInfo->TopLevelBase.ModuleId, grfscr, Js::Constants::EvalCode, fbInfo->RegisterDocument, fbInfo->IsIndirect, fbInfo->IsStrictMode);
            Assert(!pfuncScript->GetFunctionInfo()->IsGenerator());

            Js::FastEvalMapString key(source, sourceLen, fbInfo->TopLevelBase.ModuleId, fbInfo->IsStrictMode, false);
            ctx->AddToEvalMap(key, fbInfo->IsIndirect, pfuncScript);

            Js::FunctionBody* fb = JsSupport::ForceAndGetFunctionBody(pfuncScript->GetParseableFunctionInfo());

            ////
            //We don't do this automatically ing the eval helper so do it here
            TTD::NSSnapValues::TopLevelEvalFunctionBodyResolveInfo* tbfi = HeapNewStruct(TTD::NSSnapValues::TopLevelEvalFunctionBodyResolveInfo);
            TTD::NSSnapValues::ExtractTopLevelEvalFunctionBodyInfo_InScriptContext(tbfi, fb, fbInfo->TopLevelBase.ModuleId, fbInfo->TopLevelBase.SourceCode.Contents, fbInfo->TopLevelBase.SourceCode.Length, (ulong)fbInfo->EvalFlags, fbInfo->RegisterDocument, fbInfo->IsIndirect, fbInfo->IsStrictMode);
            ctx->m_ttdTopLevelEval.Add(tbfi);

            //walk global body to (1) add functions to pin set (2) build parent map
            ctx->ProcessFunctionBodyOnLoad(fb, nullptr);
            ////

            return fb;
        }

        void EmitTopLevelEvalFunctionBodyInfo(const TopLevelEvalFunctionBodyResolveInfo* fbInfo, LPCWSTR sourceDir, IOStreamFunctions& streamFunctions, FileWriter* writer, NSTokens::Separator separator)
        {
            NSSnapValues::EmitTopLevelCommonBodyResolveInfo(&fbInfo->TopLevelBase, true, sourceDir, streamFunctions, writer, separator);

            writer->WriteUInt64(NSTokens::Key::u64Val, fbInfo->EvalFlags, NSTokens::Separator::CommaSeparator);
            writer->WriteBool(NSTokens::Key::boolVal, fbInfo->RegisterDocument, NSTokens::Separator::CommaSeparator);
            writer->WriteBool(NSTokens::Key::boolVal, fbInfo->IsIndirect, NSTokens::Separator::CommaSeparator);
            writer->WriteBool(NSTokens::Key::boolVal, fbInfo->IsStrictMode, NSTokens::Separator::CommaSeparator);
            writer->WriteRecordEnd();
        }

        void ParseTopLevelEvalFunctionBodyInfo(TopLevelEvalFunctionBodyResolveInfo* fbInfo, bool readSeperator, LPCWSTR sourceDir, IOStreamFunctions& streamFunctions, FileReader* reader, SlabAllocator& alloc)
        {
            NSSnapValues::ParseTopLevelCommonBodyResolveInfo(&fbInfo->TopLevelBase, readSeperator, false, sourceDir, streamFunctions, reader, alloc);

            fbInfo->EvalFlags = reader->ReadUInt64(NSTokens::Key::u64Val, true);
            fbInfo->RegisterDocument = reader->ReadBool(NSTokens::Key::boolVal, true);
            fbInfo->IsIndirect = reader->ReadBool(NSTokens::Key::boolVal, true);
            fbInfo->IsStrictMode = reader->ReadBool(NSTokens::Key::boolVal, true);
            reader->ReadRecordEnd();
        }

#if ENABLE_SNAPSHOT_COMPARE 
        void AssertSnapEquiv(const TopLevelEvalFunctionBodyResolveInfo* fbInfo1, const TopLevelEvalFunctionBodyResolveInfo* fbInfo2, TTDCompareMap& compareMap)
        {
            compareMap.DiagnosticAssert(fbInfo1->EvalFlags == fbInfo2->EvalFlags);
            compareMap.DiagnosticAssert(fbInfo1->RegisterDocument == fbInfo2->RegisterDocument);
            compareMap.DiagnosticAssert(fbInfo1->IsIndirect == fbInfo2->IsIndirect);
            compareMap.DiagnosticAssert(fbInfo1->IsStrictMode == fbInfo2->IsStrictMode);

            AssertSnapEquiv(&(fbInfo1->TopLevelBase), &(fbInfo2->TopLevelBase), compareMap);
        }
#endif

        void ExtractFunctionBodyInfo(FunctionBodyResolveInfo* fbInfo, Js::FunctionBody* fb, bool isWellKnown, SlabAllocator& alloc)
        {
            fbInfo->FunctionBodyId = TTD_CONVERT_FUNCTIONBODY_TO_PTR_ID(fb);
            fbInfo->ScriptContextTag = TTD_EXTRACT_CTX_LOG_TAG(fb->GetScriptContext());

            alloc.CopyStringIntoWLength(fb->GetDisplayName(), fb->GetDisplayNameLength(), fbInfo->FunctionName);
            AssertMsg(wcscmp(fbInfo->FunctionName.Contents, Js::Constants::GlobalCode) != 0, "Why are we snapshotting global code??");

            if(isWellKnown)
            {
                fbInfo->OptKnownPath = alloc.CopyRawNullTerminatedStringInto(fb->GetScriptContext()->ResolveKnownTokenForRuntimeFunctionBody_TTD(fb));

                fbInfo->OptParentBodyId = TTD_INVALID_PTR_ID;
                fbInfo->OptLine = -1;
                fbInfo->OptColumn = -1;
            }
            else
            {
                fbInfo->OptKnownPath = TTD_INVALID_WELLKNOWN_TOKEN;

                Js::FunctionBody* parentBody = fb->GetScriptContext()->ResolveParentBody(fb);
                AssertMsg(parentBody != nullptr, "We missed something!!!");

                fbInfo->OptParentBodyId = TTD_CONVERT_FUNCTIONBODY_TO_PTR_ID(parentBody);
                fbInfo->OptLine = fb->GetLineNumber();
                fbInfo->OptColumn = fb->GetColumnNumber();
            }
        }

        void InflateFunctionBody(const FunctionBodyResolveInfo* fbInfo, InflateMap* inflator, const TTDIdentifierDictionary<TTD_PTR_ID, FunctionBodyResolveInfo*>& idToFbResolveMap)
        {
            if(inflator->IsFunctionBodyAlreadyInflated(fbInfo->FunctionBodyId))
            {
                return;
            }

            Js::ScriptContext* ctx = inflator->LookupScriptContext(fbInfo->ScriptContextTag);

            Js::FunctionBody* resfb = nullptr;
            if(fbInfo->OptKnownPath != TTD_INVALID_WELLKNOWN_TOKEN)
            {
                resfb = ctx->LookupRuntimeFunctionBodyForKnownToken_TTD(fbInfo->OptKnownPath);
            }
            else
            {
                resfb = inflator->FindReusableFunctionBodyIfExists(fbInfo->FunctionBodyId);
                if(resfb == nullptr)
                {
                    //Find and inflate the parent body
                    if(!inflator->IsFunctionBodyAlreadyInflated(fbInfo->OptParentBodyId))
                    {
                        const FunctionBodyResolveInfo* pBodyInfo = idToFbResolveMap.LookupKnownItem(fbInfo->OptParentBodyId);
                        InflateFunctionBody(pBodyInfo, inflator, idToFbResolveMap);
                    }

                    Js::FunctionBody* parentBody = inflator->LookupFunctionBody(fbInfo->OptParentBodyId);

                    int32 imin = 0;
                    int32 imax = parentBody->GetNestedCount();
                    while(imin < imax)
                    {
                        int imid = (imin + imax) / 2;
                        Js::ParseableFunctionInfo* pfiMid = parentBody->GetNestedFunc(imid)->EnsureDeserialized();
                        Js::FunctionBody* currfb = JsSupport::ForceAndGetFunctionBody(pfiMid);

                        int32 cmpval = 0;
                        if(fbInfo->OptLine != currfb->GetLineNumber())
                        {
                            cmpval = (fbInfo->OptLine < currfb->GetLineNumber()) ? -1 : 1;
                        }
                        else
                        {
                            if(fbInfo->OptColumn != currfb->GetColumnNumber())
                            {
                                cmpval = (fbInfo->OptColumn < currfb->GetColumnNumber()) ? -1 : 1;
                            }
                        }

                        if(cmpval == 0)
                        {
                            resfb = currfb;
                            break;
                        }

                        if(cmpval > 0)
                        {
                            imin = imid + 1;
                        }
                        else
                        {
                            imax = imid;
                        }
                    }
                    AssertMsg(resfb != nullptr && fbInfo->OptLine == resfb->GetLineNumber() && fbInfo->OptColumn == resfb->GetColumnNumber(), "We are missing something");
                    AssertMsg(resfb != nullptr && (wcscmp(fbInfo->FunctionName.Contents, resfb->GetDisplayName()) == 0 || wcscmp(L"get", resfb->GetDisplayName()) == 0 || wcscmp(L"set", resfb->GetDisplayName()) == 0), "We are missing something");
                }
            }

            bool updateName = false;
            if(fbInfo->FunctionName.Length != resfb->GetDisplayNameLength())
            {
                updateName = true;
            }
            else
            {
                LPCWSTR fname = resfb->GetDisplayName();
                for(uint32 i = 0; i < fbInfo->FunctionName.Length; ++i)
                {
                    updateName |= (fbInfo->FunctionName.Contents[i] != fname[i]);
                }
            }

            if(updateName)
            {
                AssertMsg(wcsstr(fbInfo->FunctionName.Contents, L"get ") == 0 || wcsstr(fbInfo->FunctionName.Contents, L"set ") == 0, "Does not start with get or set");

                resfb->SetDisplayName(fbInfo->FunctionName.Contents,fbInfo->FunctionName.Length, 3, Js::FunctionProxy::SetDisplayNameFlagsRecyclerAllocated);
            }

            inflator->AddInflationFunctionBody(fbInfo->FunctionBodyId, resfb);
        }

        void EmitFunctionBodyInfo(const FunctionBodyResolveInfo* fbInfo, LPCWSTR sourceDir, IOStreamFunctions& streamFunctions, FileWriter* writer, NSTokens::Separator separator)
        {
            writer->WriteRecordStart(separator);
            writer->WriteAddr(NSTokens::Key::functionBodyId, fbInfo->FunctionBodyId);
            writer->WriteLogTag(NSTokens::Key::ctxTag, fbInfo->ScriptContextTag, NSTokens::Separator::CommaSeparator);
            writer->WriteString(NSTokens::Key::name, fbInfo->FunctionName, NSTokens::Separator::CommaSeparator);

            writer->WriteBool(NSTokens::Key::isWellKnownToken, fbInfo->OptKnownPath != nullptr, NSTokens::Separator::CommaSeparator);
            if(fbInfo->OptKnownPath != TTD_INVALID_WELLKNOWN_TOKEN)
            {
                writer->WriteWellKnownToken(NSTokens::Key::wellKnownToken, fbInfo->OptKnownPath, NSTokens::Separator::CommaSeparator);
            }
            else
            {
                writer->WriteAddr(NSTokens::Key::parentBodyId, fbInfo->OptParentBodyId, NSTokens::Separator::CommaSeparator);
                writer->WriteInt64(NSTokens::Key::line, fbInfo->OptLine, NSTokens::Separator::CommaSeparator);
                writer->WriteInt64(NSTokens::Key::column, fbInfo->OptColumn, NSTokens::Separator::CommaSeparator);
            }

            writer->WriteRecordEnd();
        }

        void ParseFunctionBodyInfo(FunctionBodyResolveInfo* fbInfo, bool readSeperator, LPCWSTR sourceDir, IOStreamFunctions& streamFunctions, FileReader* reader, SlabAllocator& alloc)
        {
            reader->ReadRecordStart(readSeperator);

            fbInfo->FunctionBodyId = reader->ReadAddr(NSTokens::Key::functionBodyId);
            fbInfo->ScriptContextTag = reader->ReadLogTag(NSTokens::Key::ctxTag, true);
            reader->ReadString(NSTokens::Key::name, alloc, fbInfo->FunctionName, true);

            bool isWellKnown = reader->ReadBool(NSTokens::Key::isWellKnownToken, true);
            if(isWellKnown)
            {
                fbInfo->OptKnownPath = reader->ReadWellKnownToken(NSTokens::Key::wellKnownToken, alloc, true);

                fbInfo->OptParentBodyId = TTD_INVALID_PTR_ID;
                fbInfo->OptLine = -1;
                fbInfo->OptColumn = -1;
            }
            else
            {
                fbInfo->OptKnownPath = TTD_INVALID_WELLKNOWN_TOKEN;

                fbInfo->OptParentBodyId = reader->ReadAddr(NSTokens::Key::parentBodyId, true);
                fbInfo->OptLine = reader->ReadInt64(NSTokens::Key::line, true);
                fbInfo->OptColumn = reader->ReadInt64(NSTokens::Key::column, true);
            }

            reader->ReadRecordEnd();
        }

#if ENABLE_SNAPSHOT_COMPARE 
        void AssertSnapEquiv(const FunctionBodyResolveInfo* fbInfo1, const FunctionBodyResolveInfo* fbInfo2, TTDCompareMap& compareMap)
        {
            compareMap.DiagnosticAssert(fbInfo1->ScriptContextTag == fbInfo2->ScriptContextTag);
            compareMap.DiagnosticAssert(TTStringEQForDiagnostics(fbInfo1->FunctionName, fbInfo2->FunctionName));

            compareMap.DiagnosticAssert(TTD_DIAGNOSTIC_COMPARE_WELLKNOWN_TOKENS(fbInfo1->OptKnownPath, fbInfo2->OptKnownPath));
            if(fbInfo1->OptKnownPath == TTD_INVALID_WELLKNOWN_TOKEN)
            {
                compareMap.CheckConsistentAndAddPtrIdMapping_FunctionBody(fbInfo1->OptParentBodyId, fbInfo2->OptParentBodyId);

                compareMap.DiagnosticAssert(fbInfo1->OptLine == fbInfo2->OptLine);
                compareMap.DiagnosticAssert(fbInfo1->OptColumn == fbInfo2->OptColumn);
            }
        }
#endif

        //////////////////

        void ExtractScriptContext(SnapContext* snapCtx, Js::ScriptContext* ctx, SlabAllocator& alloc)
        {
            snapCtx->m_scriptContextTagId = TTD_EXTRACT_CTX_LOG_TAG(ctx);

            snapCtx->m_randomSeed0 = ctx->GetLibrary()->GetRandSeed0();
            snapCtx->m_randomSeed1 = ctx->GetLibrary()->GetRandSeed1();
            alloc.CopyNullTermStringInto(ctx->GetUrl(), snapCtx->m_contextSRC);

            JsUtil::List<NSSnapValues::TopLevelScriptLoadFunctionBodyResolveInfo*, HeapAllocator> topLevelScriptLoad(&HeapAllocator::Instance); 
            JsUtil::List<NSSnapValues::TopLevelNewFunctionBodyResolveInfo*, HeapAllocator> topLevelNewFunction(&HeapAllocator::Instance); 
            JsUtil::List<NSSnapValues::TopLevelEvalFunctionBodyResolveInfo*, HeapAllocator> topLevelEval(&HeapAllocator::Instance);

            ctx->GetLoadedSources_TTD(topLevelScriptLoad, topLevelNewFunction, topLevelEval);

            snapCtx->m_loadedScriptCount = topLevelScriptLoad.Count();
            snapCtx->m_loadedScriptArray = (snapCtx->m_loadedScriptCount != 0) ? alloc.SlabAllocateArray<TopLevelScriptLoadFunctionBodyResolveInfo>(snapCtx->m_loadedScriptCount) : nullptr;
            for(int32 i = 0; i < topLevelScriptLoad.Count(); ++i)
            {
                NSSnapValues::ExtractTopLevelLoadedFunctionBodyInfo_InShapshot(snapCtx->m_loadedScriptArray + i, topLevelScriptLoad.Item(i), alloc);
            }

            snapCtx->m_newScriptCount = topLevelNewFunction.Count();
            snapCtx->m_newScriptArray = (snapCtx->m_newScriptCount != 0) ? alloc.SlabAllocateArray<TopLevelNewFunctionBodyResolveInfo>(snapCtx->m_newScriptCount) : nullptr;
            for(int32 i = 0; i < topLevelNewFunction.Count(); ++i)
            {
                NSSnapValues::ExtractTopLevelNewFunctionBodyInfo_InShapshot(snapCtx->m_newScriptArray + i, topLevelNewFunction.Item(i), alloc);
            }

            snapCtx->m_evalScriptCount = topLevelEval.Count();
            snapCtx->m_evalScriptArray = (snapCtx->m_evalScriptCount != 0) ? alloc.SlabAllocateArray<TopLevelEvalFunctionBodyResolveInfo>(snapCtx->m_evalScriptCount) : nullptr;
            for(int32 i = 0; i < topLevelEval.Count(); ++i)
            {
                NSSnapValues::ExtractTopLevelEvalFunctionBodyInfo_InShapshot(snapCtx->m_evalScriptArray + i, topLevelEval.Item(i), alloc);
            }

            snapCtx->m_rootCount = ctx->m_ttdRootSet->Count();
            snapCtx->m_rootArray = (snapCtx->m_rootCount != 0) ? alloc.SlabAllocateArray<TTD_PTR_ID>(snapCtx->m_rootCount) : nullptr;

            int32 i = 0;
            for(auto iter = ctx->m_ttdRootSet->GetIterator(); iter.IsValid(); iter.MoveNext())
            {
                Js::RecyclableObject* robj = static_cast<Js::RecyclableObject*>(iter.CurrentKey());
                snapCtx->m_rootArray[i] = TTD_CONVERT_VAR_TO_PTR_ID(robj);

                i++;
            }
        }

        void InflateScriptContext(const SnapContext* snpCtx, Js::ScriptContext* intoCtx, InflateMap* inflator)
        {
            AssertMsg(wcscmp(snpCtx->m_contextSRC.Contents, intoCtx->GetUrl()) == 0, "Make sure the src uri values are the same.");

            intoCtx->GetLibrary()->SetRandSeed0(snpCtx->m_randomSeed0);
            intoCtx->GetLibrary()->SetRandSeed1(snpCtx->m_randomSeed1);
            inflator->AddScriptContext(snpCtx->m_scriptContextTagId, intoCtx);

            for(uint32 i = 0; i < snpCtx->m_loadedScriptCount; ++i)
            {
                TopLevelScriptLoadFunctionBodyResolveInfo* fbInfo = snpCtx->m_loadedScriptArray + i;
                Js::FunctionBody* fb = inflator->FindReusableFunctionBodyIfExists(fbInfo->TopLevelBase.FunctionBodyId);
                if(fb == nullptr)
                {
                    fb = NSSnapValues::InflateTopLevelLoadedFunctionBodyInfo(fbInfo, intoCtx);
                }
                inflator->AddInflationFunctionBody(fbInfo->TopLevelBase.FunctionBodyId, fb);
            }

            for(uint32 i = 0; i < snpCtx->m_newScriptCount; ++i)
            {
                TopLevelNewFunctionBodyResolveInfo* fbInfo = snpCtx->m_newScriptArray + i;
                Js::FunctionBody* fb = inflator->FindReusableFunctionBodyIfExists(fbInfo->TopLevelBase.FunctionBodyId);
                if(fb == nullptr)
                {
                    fb = NSSnapValues::InflateTopLevelNewFunctionBodyInfo(fbInfo, intoCtx);
                }
                inflator->AddInflationFunctionBody(fbInfo->TopLevelBase.FunctionBodyId, fb);
            }

            for(uint32 i = 0; i < snpCtx->m_evalScriptCount; ++i)
            {
                TopLevelEvalFunctionBodyResolveInfo* fbInfo = snpCtx->m_evalScriptArray + i;
                Js::FunctionBody* fb = inflator->FindReusableFunctionBodyIfExists(fbInfo->TopLevelBase.FunctionBodyId);
                if(fb == nullptr)
                {
                    fb = NSSnapValues::InflateTopLevelEvalFunctionBodyInfo(fbInfo, intoCtx);
                }
                inflator->AddInflationFunctionBody(fbInfo->TopLevelBase.FunctionBodyId, fb);
            }
        }

        void ReLinkRoots(const SnapContext* snpCtx, Js::ScriptContext* intoCtx, InflateMap* inflator)
        {
            intoCtx->ClearRootsForSnapRestore_TTD();

            for(uint32 i = 0; i < snpCtx->m_rootCount; ++i)
            {
                TTD_PTR_ID oid = snpCtx->m_rootArray[i];
                Js::RecyclableObject* robj = inflator->LookupObject(oid);

                intoCtx->m_ttdRootSet->AddNew(robj);
            }
        }

        void EmitSnapContext(const SnapContext* snapCtx, LPCWSTR sourceDir, IOStreamFunctions& streamFunctions, FileWriter* writer, NSTokens::Separator separator)
        {
            writer->WriteRecordStart(separator);

            writer->WriteLogTag(NSTokens::Key::ctxTag, snapCtx->m_scriptContextTagId);
            writer->WriteUInt64(NSTokens::Key::u64Val, snapCtx->m_randomSeed0, NSTokens::Separator::CommaSeparator);
            writer->WriteUInt64(NSTokens::Key::u64Val, snapCtx->m_randomSeed1, NSTokens::Separator::CommaSeparator);
            writer->WriteString(NSTokens::Key::ctxUri, snapCtx->m_contextSRC, NSTokens::Separator::CommaSeparator);

            writer->WriteLengthValue(snapCtx->m_loadedScriptCount, NSTokens::Separator::CommaSeparator);
            writer->WriteSequenceStart_DefaultKey(NSTokens::Separator::CommaSeparator);
            for(uint32 i = 0; i < snapCtx->m_loadedScriptCount; ++i)
            {
                NSTokens::Separator sep = (i != 0) ? NSTokens::Separator::CommaSeparator : NSTokens::Separator::NoSeparator;
                NSSnapValues::EmitTopLevelLoadedFunctionBodyInfo(snapCtx->m_loadedScriptArray + i, sourceDir, streamFunctions, writer, sep);
            }
            writer->WriteSequenceEnd();

            writer->WriteLengthValue(snapCtx->m_newScriptCount, NSTokens::Separator::CommaSeparator);
            writer->WriteSequenceStart_DefaultKey(NSTokens::Separator::CommaSeparator);
            for(uint32 i = 0; i < snapCtx->m_newScriptCount; ++i)
            {
                NSTokens::Separator sep = (i != 0) ? NSTokens::Separator::CommaSeparator : NSTokens::Separator::NoSeparator;
                NSSnapValues::EmitTopLevelNewFunctionBodyInfo(snapCtx->m_newScriptArray + i, sourceDir, streamFunctions, writer, sep);
            }
            writer->WriteSequenceEnd();

            writer->WriteLengthValue(snapCtx->m_evalScriptCount, NSTokens::Separator::CommaSeparator);
            writer->WriteSequenceStart_DefaultKey(NSTokens::Separator::CommaSeparator);
            for(uint32 i = 0; i < snapCtx->m_evalScriptCount; ++i)
            {
                NSTokens::Separator sep = (i != 0) ? NSTokens::Separator::CommaSeparator : NSTokens::Separator::NoSeparator;
                NSSnapValues::EmitTopLevelEvalFunctionBodyInfo(snapCtx->m_evalScriptArray + i, sourceDir, streamFunctions, writer, sep);
            }
            writer->WriteSequenceEnd();

            writer->WriteLengthValue(snapCtx->m_rootCount, NSTokens::Separator::CommaSeparator);
            writer->WriteSequenceStart_DefaultKey(NSTokens::Separator::CommaSeparator);
            for(uint32 i = 0; i < snapCtx->m_rootCount; ++i)
            {
                NSTokens::Separator sep = (i != 0) ? NSTokens::Separator::CommaSeparator : NSTokens::Separator::NoSeparator;
                writer->WriteNakedAddr(snapCtx->m_rootArray[i], sep);
            }
            writer->WriteSequenceEnd();

            writer->WriteRecordEnd();
        }

        void ParseSnapContext(SnapContext* intoCtx, bool readSeperator, LPCWSTR sourceDir, IOStreamFunctions& streamFunctions, FileReader* reader, SlabAllocator& alloc)
        {
            reader->ReadRecordStart(readSeperator);

            intoCtx->m_scriptContextTagId = reader->ReadLogTag(NSTokens::Key::ctxTag);
            intoCtx->m_randomSeed0 = reader->ReadUInt64(NSTokens::Key::u64Val, true);
            intoCtx->m_randomSeed1 = reader->ReadUInt64(NSTokens::Key::u64Val, true);
            reader->ReadString(NSTokens::Key::ctxUri, alloc, intoCtx->m_contextSRC, true);

            intoCtx->m_loadedScriptCount = reader->ReadLengthValue(true);
            intoCtx->m_loadedScriptArray = (intoCtx->m_loadedScriptCount != 0) ? alloc.SlabAllocateArray<TopLevelScriptLoadFunctionBodyResolveInfo>(intoCtx->m_loadedScriptCount) : nullptr;
            reader->ReadSequenceStart_WDefaultKey(true);
            for(uint32 i = 0; i < intoCtx->m_loadedScriptCount; ++i)
            {
                NSSnapValues::ParseTopLevelLoadedFunctionBodyInfo(intoCtx->m_loadedScriptArray + i, i != 0, sourceDir, streamFunctions, reader, alloc);
            }
            reader->ReadSequenceEnd();

            intoCtx->m_newScriptCount = reader->ReadLengthValue(true);
            intoCtx->m_newScriptArray = (intoCtx->m_newScriptCount != 0) ? alloc.SlabAllocateArray<TopLevelNewFunctionBodyResolveInfo>(intoCtx->m_newScriptCount) : nullptr;
            reader->ReadSequenceStart_WDefaultKey(true);
            for(uint32 i = 0; i < intoCtx->m_newScriptCount; ++i)
            {
                NSSnapValues::ParseTopLevelNewFunctionBodyInfo(intoCtx->m_newScriptArray + i, i != 0, sourceDir, streamFunctions, reader, alloc);
            }
            reader->ReadSequenceEnd();

            intoCtx->m_evalScriptCount = reader->ReadLengthValue(true);
            intoCtx->m_evalScriptArray = (intoCtx->m_evalScriptCount != 0) ? alloc.SlabAllocateArray<TopLevelEvalFunctionBodyResolveInfo>(intoCtx->m_evalScriptCount) : nullptr;
            reader->ReadSequenceStart_WDefaultKey(true);
            for(uint32 i = 0; i < intoCtx->m_evalScriptCount; ++i)
            {
                NSSnapValues::ParseTopLevelEvalFunctionBodyInfo(intoCtx->m_evalScriptArray + i, i != 0, sourceDir, streamFunctions, reader, alloc);
            }
            reader->ReadSequenceEnd();

            intoCtx->m_rootCount = reader->ReadLengthValue(true);
            intoCtx->m_rootArray = (intoCtx->m_rootCount != 0) ? alloc.SlabAllocateArray<TTD_PTR_ID>(intoCtx->m_rootCount) : nullptr;

            reader->ReadSequenceStart_WDefaultKey(true);
            for(uint32 i = 0; i < intoCtx->m_rootCount; ++i)
            {
                intoCtx->m_rootArray[i] = reader->ReadNakedAddr(i != 0);
            }
            reader->ReadSequenceEnd();

            reader->ReadRecordEnd();
        }

#if ENABLE_SNAPSHOT_COMPARE 
        void AssertSnapEquiv(const SnapContext* snapCtx1, const SnapContext* snapCtx2, TTDCompareMap& compareMap)
        {
            compareMap.DiagnosticAssert(snapCtx1->m_scriptContextTagId == snapCtx2->m_scriptContextTagId);

            compareMap.DiagnosticAssert(snapCtx1->m_randomSeed0 == snapCtx2->m_randomSeed0);
            compareMap.DiagnosticAssert(snapCtx1->m_randomSeed1 == snapCtx2->m_randomSeed1);

            compareMap.DiagnosticAssert(TTStringEQForDiagnostics(snapCtx1->m_contextSRC, snapCtx2->m_contextSRC));

            //
            //TODO: Once loaded script has a unique identifier we can match (e.g. documentId) then we should match here.
            //      For now just sanity check the number of top-level functions and let the FunctionBody matching drive any matching.
            //

            compareMap.DiagnosticAssert(snapCtx1->m_loadedScriptCount == snapCtx2->m_loadedScriptCount);
            //TopLevelScriptLoadFunctionBodyResolveInfo* m_loadedScriptArray;

            compareMap.DiagnosticAssert(snapCtx1->m_newScriptCount == snapCtx2->m_newScriptCount);
            //TopLevelNewFunctionBodyResolveInfo* m_newScriptArray;

            compareMap.DiagnosticAssert(snapCtx1->m_evalScriptCount == snapCtx2->m_evalScriptCount);
            //TopLevelEvalFunctionBodyResolveInfo* m_evalScriptArray;

            compareMap.DiagnosticAssert(snapCtx1->m_rootCount == snapCtx2->m_rootCount);

            JsUtil::BaseDictionary<TTD_LOG_TAG, TTD_PTR_ID, HeapAllocator> rootMap1(&HeapAllocator::Instance);
            for(uint32 i = 0; i < snapCtx1->m_rootCount; ++i)
            {
                TTD_LOG_TAG ltag = compareMap.H1TagMap.Lookup(snapCtx1->m_rootArray[i], TTD_INVALID_LOG_TAG);
                AssertMsg(ltag != TTD_INVALID_LOG_TAG, "Missing log tag!!!");

                rootMap1.AddNew(ltag, snapCtx1->m_rootArray[i]);
            }

            for(uint32 i = 0; i < snapCtx2->m_rootCount; ++i)
            {
                TTD_PTR_ID h2PtrId = snapCtx2->m_rootArray[i];
                TTD_LOG_TAG h2LogTag = compareMap.H2TagMap.Lookup(h2PtrId, TTD_INITIAL_LOG_TAG);
                AssertMsg(h2LogTag != TTD_INVALID_LOG_TAG, "Missing log tag!!!");

                TTD_PTR_ID h1PtrId = rootMap1.Lookup(h2LogTag, TTD_INVALID_PTR_ID);
                AssertMsg(h1PtrId != TTD_INVALID_PTR_ID, "Missing pointer id!!!");

                compareMap.CheckConsistentAndAddPtrIdMapping_Root(h1PtrId, h2PtrId, h2LogTag);
            }
        }
#endif
    }
}

#endif