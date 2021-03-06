/******************************************************************************
** Copyright (c) 2013-2015 Intel Corporation All Rights Reserved
**
** Licensed under the Apache License, Version 2.0 (the "License"); you may not
** use this file except in compliance with the License.
**
** You may obtain a copy of the License at
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
** WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**
** See the License for the specific language governing permissions and
** limitations under the License.
**
******************************************************************************/

#include "esif_uf_version.h"
#include "esif_uf.h"		/* Upper Framework */
#include "esif_pm.h"		/* Upper Participant Manager */
#include "esif_uf_cfgmgr.h"	/* Config Manager */
#include "esif_uf_eventmgr.h" /* Event Manager */
#include "esif_uf_primitive.h" /* Execute Primitive */

#define _DATABANK_CLASS
#include "esif_lib_databank.h"
#include "esif_dsp.h"
#include "esif_uf_tableobject.h"

#ifdef ESIF_ATTR_OS_WINDOWS
//
// The Windows banned-API check header must be included after all other headers, or issues can be identified
// against Windows SDK/DDK included headers which we have no control over.
//
#define _SDL_BANNED_RECOMMENDED
#include "win\banned.h"
#endif

#define MAX_TABLEOBJECT_KEY_LEN 256
#define VARIANT_MAX_FIELDS 50
#define COLUMN_MAX_SIZE 64
#define REVISION_INDICATOR_LENGTH 2
#define ACPI_NAME_TARGET_SIZE 4

#pragma pack(push, 1)
struct esif_data_binary_bst_package {
	union esif_data_variant battery_state;
	union esif_data_variant battery_present_rate;
	union esif_data_variant battery_remaining_capacity;
	union esif_data_variant battery_present_voltage;
};

struct guid_t {
	u32 data1;
	u16 data2;
	u16 data3;
	u8 data4[8];
};
#pragma pack(pop)

void TableField_Construct(
	TableField *self,
	char *name,
	char *label,
	EsifDataType dataType,
	int notForXML
	)
{
	esif_ccb_memset(self, 0, sizeof(*self));
	self->name = name;
	self->label = label;
	self->dataType = dataType;
	self->notForXML = notForXML;
}

void TableField_ConstructAs(
	TableField *self,
	char *name,
	char *label,
	EsifDataType dataType,
	int notForXML,
	int getPrimitive,
	int setPrimitive,
	UInt8 instance,
	char *dataVaultKey
	)
{
	esif_ccb_memset(self, 0, sizeof(*self));
	self->name = name;
	self->label = label;
	self->dataType = dataType;
	self->notForXML = notForXML;
	self->getPrimitive = getPrimitive;
	self->setPrimitive = setPrimitive;
	self->instance = instance;
	self->dataVaultKey = dataVaultKey;
}

void TableField_Destroy(TableField *self)
{
	esif_ccb_memset(self, 0, sizeof(*self));
}

eEsifError TableObject_NoPersist(
	TableObject *self,
	char *namesp,
	char *keyname
	)
{
	eEsifError rc = ESIF_OK;

	// Delete any "nopersist" Override DataVault keys for this primitive?
	if (FLAGS_TEST(self->options, TABLEOPT_HAS_NOPERSIST) && keyname && esif_ccb_strnicmp(keyname, "/participants/", 14) == 0) {
		EsifDataPtr data_nspace = NULL;
		EsifDataPtr data_path = NULL;
		char temp_key[MAX_TABLEOBJECT_KEY_LEN] = { 0 };

		esif_ccb_sprintf(sizeof(temp_key), temp_key, "/nopersist/%s", keyname + 14);
		data_nspace = EsifData_CreateAs(ESIF_DATA_STRING, (void *)(namesp ? namesp : "OVERRIDE"), 0, ESIFAUTOLEN);
		data_path = EsifData_CreateAs(ESIF_DATA_STRING, temp_key, 0, ESIFAUTOLEN);
		if (data_nspace && data_path) {
			rc = EsifConfigSet(data_nspace, data_path, ESIF_SERVICE_CONFIG_DELETE, NULL);
		}
		EsifData_Destroy(data_nspace);
		EsifData_Destroy(data_path);
	}
	return rc;
}

eEsifError TableObject_Save(TableObject *self)
{
	int i = 0;
	int numFields = 0;
	char *textInput;
	EsifDataType objDataType;
	char *tableRow = NULL;
	char *rowTok = NULL;
	char *tableCol = NULL;
	char *tmp;
	char *colTok = NULL;
	char rowDelims [] = "!";
	char colDelims [] = ",";
	eEsifError rc = ESIF_OK;
	EsifDataPtr data_nspace = NULL;
	EsifDataPtr data_path = NULL;
	EsifDataPtr data_value = NULL;
	EsifDataPtr data_key = NULL;
	esif_flags_t options = ESIF_SERVICE_CONFIG_PERSIST | ESIF_SERVICE_CONFIG_NOCACHE;

	textInput = self->dataText;
	numFields = self->numFields;
	objDataType = self->dataType;

	if (objDataType == ESIF_DATA_BINARY) {
		rc = TableObject_Convert(self);
		
		if (rc != ESIF_OK) {
			goto exit;
		}
		
		if (self->binaryDataSize) {
			struct esif_data request = { ESIF_DATA_BINARY, self->binaryData, self->binaryDataSize, self->binaryDataSize };
			struct esif_data response = { ESIF_DATA_VOID, NULL, 0, 0 };

			if (self->dataVaultNamespace) {
				u8 *targetData = esif_ccb_malloc(self->binaryDataSize);

				data_nspace = EsifData_CreateAs(ESIF_DATA_STRING, self->dataVaultNamespace, 0, ESIFAUTOLEN);
				data_path = EsifData_CreateAs(ESIF_DATA_STRING, self->dataVaultKey, 0, ESIFAUTOLEN);
				data_value = EsifData_CreateAs(ESIF_DATA_BINARY, targetData, self->binaryDataSize, self->binaryDataSize);

				if (data_nspace && data_path && data_value && targetData) {
					esif_ccb_memcpy((u8 *)targetData, (u8 *)self->binaryData, self->binaryDataSize);
					rc = EsifConfigSet(data_nspace, data_path, options, data_value);
				}
				else {
					esif_ccb_free(targetData);
					rc = ESIF_E_NO_MEMORY;
				}

			}
			else {
				TableObject_NoPersist(self, NULL, self->dataVaultKey);
				rc = EsifExecutePrimitive((UInt8) self->participantId, self->setPrimitive, self->domainQualifier, 255, &request, &response);
			}
		}
		else {
			rc = ESIF_E_UNSPECIFIED;
		}
	}
	/* These are virtual tables, which are collections of potentially unrelated primitives, grouped
	together to form a data set */
	else {
		UInt32 numberHolder32 = 0;
		UInt64 numberHolder64 = 0;
		int rowCounter = -1;
		char stringHolder[COLUMN_MAX_SIZE] = { 0 };
		struct esif_data request = { ESIF_DATA_VOID };
		struct esif_data response = { ESIF_DATA_VOID, NULL, 0, 0 };

		/* Wipe any table-level datavault keys so that new key table can be regenerated*/
		if (self->dataVaultKey != NULL) {
			options = ESIF_SERVICE_CONFIG_DELETE;
			
			data_nspace = EsifData_CreateAs(ESIF_DATA_STRING, self->dataVaultNamespace, 0, ESIFAUTOLEN);
			data_key = EsifData_CreateAs(ESIF_DATA_STRING, self->dataVaultKey, 0, ESIFAUTOLEN);
			data_value = EsifData_Create();

			if (data_nspace == NULL || data_key == NULL || data_value == NULL) {
				rc = ESIF_E_NO_MEMORY;
				goto exit;
			}

			TableObject_NoPersist(self, (char *) data_nspace->buf_ptr, (char *) data_key->buf_ptr);
			rc = EsifConfigSet(data_nspace, data_key, options, data_value);

		}

		tableRow = esif_ccb_strtok(textInput, rowDelims, &rowTok);
		while (tableRow != NULL) {
			i = -1;
			rowCounter++;
			tableCol = esif_ccb_strtok(tableRow, colDelims, &colTok);
			while (tableCol != NULL) {
				u64 colValueLen = esif_ccb_strlen(tableCol, COLUMN_MAX_SIZE) + 1;
				UNREFERENCED_PARAMETER(colValueLen);
				i++;
				if (i < numFields) {
					if ((tmp = esif_ccb_strstr(tableCol, "'")) != NULL) {
						*tmp = 0;
					}
					if (self->fields[i].setPrimitive > 0) {
						request.type = self->fields[i].dataType;
						switch (self->fields[i].dataType) {
						case ESIF_DATA_STRING:
							//tableCol holds value
							break;
						case ESIF_DATA_UINT32:
						case ESIF_DATA_POWER:
						case ESIF_DATA_TEMPERATURE:
							numberHolder32 = (UInt32) esif_atoi(tableCol);
							request.buf_len = sizeof(numberHolder32);
							request.data_len = sizeof(numberHolder32);
							request.buf_ptr = &numberHolder32;
							rc = EsifExecutePrimitive((UInt8) self->participantId, self->fields[i].setPrimitive, self->domainQualifier, self->fields[i].instance, &request, &response);
							break;
						case ESIF_DATA_UINT64:
							numberHolder64 = (UInt64) esif_atoi(tableCol);
							request.buf_len = sizeof(numberHolder64);
							request.data_len = sizeof(numberHolder64);
							request.buf_ptr = &numberHolder64;
							rc = EsifExecutePrimitive((UInt8) self->participantId, self->fields[i].setPrimitive, self->domainQualifier, self->fields[i].instance, &request, &response);
							break;
						default:
							ESIF_TRACE_DEBUG("Field type in schema for table %s is not handled \n", self->name);
							rc = ESIF_E_PARAMETER_IS_OUT_OF_BOUNDS;
							goto exit;
							break;
						}
					}
					else {  //config keys only - no corresponding primitive
						char enumeratedKey[MAX_TABLEOBJECT_KEY_LEN] = { 0 };

						options = ESIF_SERVICE_CONFIG_PERSIST | ESIF_SERVICE_CONFIG_NOCACHE;

						if (self->fields[i].dataVaultKey == NULL) {
							rc = ESIF_E_NOT_IMPLEMENTED;
							goto exit;
						}
						data_value = EsifData_Create();		

						esif_ccb_sprintf(MAX_TABLEOBJECT_KEY_LEN, enumeratedKey, "%s/%d", self->fields[i].dataVaultKey, rowCounter + 1);
						esif_ccb_sprintf(COLUMN_MAX_SIZE, stringHolder, "%s", tableCol);

						rc = EsifData_FromString(data_value, stringHolder, ESIF_DATA_STRING);

						if (rc != ESIF_OK) {
							goto exit;
						}

						EsifData_Destroy(data_nspace);
						EsifData_Destroy(data_key);
						data_nspace = EsifData_CreateAs(ESIF_DATA_STRING, self->dataVaultNamespace, 0, ESIFAUTOLEN);
						data_key = EsifData_CreateAs(ESIF_DATA_STRING, enumeratedKey, 0, ESIFAUTOLEN);
						
						if (data_nspace == NULL || data_key == NULL || data_value == NULL) {
							rc = ESIF_E_NO_MEMORY;
							goto exit;
						}

						rc = EsifConfigSet(data_nspace, data_key, options, data_value);

					}
				}
				// Get next column
				tableCol = esif_ccb_strtok(NULL, colDelims, &colTok);
			}
			// Get next row
			tableRow = esif_ccb_strtok(NULL, rowDelims, &rowTok);
		}
	}

exit:
	EsifData_Destroy(data_nspace);
	EsifData_Destroy(data_path);
	EsifData_Destroy(data_value);
	EsifData_Destroy(data_key);

	return rc;
}

eEsifError TableObject_LoadXML(
	TableObject *self
	)
{
	int i = 1;
	EsifDataType targetType;
	u8 *primResponse;
	char *output = esif_ccb_malloc(OUT_BUF_LEN);
	union esif_data_variant *obj;
	int remain_bytes = 0;
	char *strFieldValue = NULL;
	u32 int32FieldValue = 0;
	u64 int64FieldValue = 0;
	eEsifError rc = ESIF_OK;
	eEsifError primitiveOK = ESIF_OK;  /* for use with virtual tables, nonfatal errors */
	u8 *tmp_buf = (u8 *) esif_ccb_malloc(OUT_BUF_LEN);
	UInt32 defaultNumber = 0;
	EsifDataType objDataType = self->dataType;
	struct esif_data request = { ESIF_DATA_VOID, NULL, 0, 0 };
	struct esif_data response = { ESIF_DATA_VOID };
	EsifDataPtr  data_nspace = NULL;
	EsifDataPtr  data_key = NULL;

	if ((NULL == tmp_buf) || (NULL == output)) {
		rc = ESIF_E_NO_MEMORY;
		goto exit;
	}
	*output = '\0';

	response.data_len = 0;

	if (objDataType == ESIF_DATA_BINARY) {
		response.type = objDataType;
		response.buf_ptr = tmp_buf;
		response.buf_len = OUT_BUF_LEN;
		if (!self->binaryData){
			rc = EsifExecutePrimitive((UInt8) self->participantId, self->getPrimitive, self->domainQualifier, 255, &request, &response);
			if (ESIF_OK != rc) {
				goto exit;
			}
			primResponse = (u8 *) response.buf_ptr;
			remain_bytes = response.data_len;
		}
		else {
			primResponse = (u8 *) self->binaryData;
			remain_bytes = self->binaryDataSize;
		}
		obj = (union esif_data_variant *)primResponse;
		
		esif_ccb_sprintf(OUT_BUF_LEN, output, "<result>\n");

		/* if the table has a revision, load that in and shift the bytes
		before looping through the fields */
		if (FLAGS_TEST(self->options, TABLEOPT_CONTAINS_REVISION)) {
			int64FieldValue = (u64) obj->integer.value;
			obj = (union esif_data_variant *)((u8 *) obj + sizeof(*obj));
			remain_bytes -= sizeof(*obj);
			esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "<revision>%llu</revision>\n", int64FieldValue);
		}
		/* TODO: Check the value loaded in the revision number against the supported
		revision list, and reload the schema if a different revision is needed
		(and is available */

		/* loop through the fields that were provided by _LoadSchema */
		while (remain_bytes >= sizeof(*obj)) {
			esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "  <trtRow>\n");
			for (i = 0; i < self->numFields && remain_bytes >= sizeof(*obj); i++) {
				remain_bytes -= sizeof(*obj);
				ESIF_TRACE_DEBUG("Obtaining bios binary data for table: %s, field: %s, type: %d \n", self->name, self->fields[i].name, self->fields[i].dataType);
				targetType = (FLAGS_TEST(self->options, TABLEOPT_ALLOW_SELF_DEFINE) ? obj->type : self->fields[i].dataType);
				switch (targetType) {
				case ESIF_DATA_STRING:
					if (obj->type != ESIF_DATA_STRING) {
						ESIF_TRACE_DEBUG("While loading field: %s into table: %s, field datatype mismatch detected (expecting STRING). \n", self->fields[i].name, self->name);
						rc = ESIF_E_PARAMETER_IS_OUT_OF_BOUNDS;
						goto exit;
					}
					strFieldValue = (char *) ((u8 *) obj + sizeof(*obj));
					remain_bytes -= obj->string.length;
					obj = (union esif_data_variant *)((u8 *) obj + (sizeof(*obj) + obj->string.length));
					ESIF_TRACE_DEBUG("Determined field value: %s \n", strFieldValue);
					esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "    <%s>%s</%s>\n", self->fields[i].name, strFieldValue, self->fields[i].name);
					break;
				case ESIF_DATA_UINT32:
					if (obj->type != ESIF_DATA_UINT32) {
						ESIF_TRACE_DEBUG("While loading field: %s into table: %s, field datatype mismatch detected (expecting 32 bit integer). \n", self->fields[i].name, self->name);
						rc = ESIF_E_PARAMETER_IS_OUT_OF_BOUNDS;
						goto exit;
					}
					int32FieldValue = (u32) obj->integer.value;
					obj = (union esif_data_variant *)((u8 *) obj + sizeof(*obj));
					ESIF_TRACE_DEBUG("Determined field value: %d \n", int32FieldValue);
					esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "    <%s>%d</%s>\n", self->fields[i].name, int32FieldValue, self->fields[i].name);
					break;
				case ESIF_DATA_UINT64:
					//UInt32's are allowed - values will be casted up
					if (obj->type != ESIF_DATA_UINT32 && obj->type != ESIF_DATA_UINT64) {
						ESIF_TRACE_DEBUG("While loading field: %s into table: %s, field datatype mismatch detected (expecting 64 bit integer, 32 bit okay). \n", self->fields[i].name, self->name);
						rc = ESIF_E_PARAMETER_IS_OUT_OF_BOUNDS;
						goto exit;
					}
					int64FieldValue = (u64) obj->integer.value;
					obj = (union esif_data_variant *)((u8 *) obj + sizeof(*obj));
					ESIF_TRACE_DEBUG("Determined field value: %lld \n", int64FieldValue);
					esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "    <%s>%lld</%s>\n", self->fields[i].name, int64FieldValue, self->fields[i].name);
					break;
				default:
					ESIF_TRACE_DEBUG("Field type in schema for table %s is not handled \n", self->name);
					rc = ESIF_E_PARAMETER_IS_OUT_OF_BOUNDS;
					goto exit;
					break;
				}
			}
			esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "  </trtRow>\n");
		}

		esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "</result>\n");
	}
	/* these are tables created out of datavault keys */
	else if (objDataType == ESIF_DATA_STRING) {
		esif_ccb_sprintf(OUT_BUF_LEN, output, "<result>\n");
		
		for (i = 0; i < self->numFields; i++) {
			char keyname[MAX_TABLEOBJECT_KEY_LEN] = { 0 };
			EsifConfigFindContext context = NULL;

			if (self->fields[i].dataVaultKey == NULL) {
				rc = ESIF_E_NOT_IMPLEMENTED;
				goto exit;
			}

			esif_ccb_sprintf(MAX_TABLEOBJECT_KEY_LEN, keyname, "%s/*", self->fields[i].dataVaultKey);
			data_nspace = EsifData_CreateAs(ESIF_DATA_STRING, self->dataVaultNamespace, 0, ESIFAUTOLEN);
			data_key = EsifData_CreateAs(ESIF_DATA_STRING, keyname, 0, ESIFAUTOLEN);

			if (data_nspace == NULL || data_key == NULL) {
				rc = ESIF_E_NO_MEMORY;
				goto exit;
			}

			if ((rc = EsifConfigFindFirst(data_nspace, data_key, NULL, &context)) == ESIF_OK) {
				do {
					EsifDataPtr  data_value = NULL;
					data_value = EsifData_CreateAs(self->fields[i].dataType, NULL, ESIF_DATA_ALLOCATE, 0);
					rc = EsifConfigGet(data_nspace, data_key, data_value);
					if (rc == ESIF_OK) { //these only have a single field until there is a need to span across keys
						esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "  <tableRow>\n");
						esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "    <%s>%s</%s>\n", self->fields[i].name, data_value->buf_ptr, self->fields[i].name);
						esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "  </tableRow>\n");
					}
					EsifData_Set(data_key, ESIF_DATA_STRING, keyname, 0, ESIFAUTOLEN);
					EsifData_Destroy(data_value);
				} while ((rc = EsifConfigFindNext(data_nspace, data_key, NULL, &context)) == ESIF_OK);
				if (rc == ESIF_E_ITERATION_DONE)
					rc = ESIF_OK;
			}
		}
		
		esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "</result>\n");
	}
	/* these are virtual tables (collections of individual primitives, grouped together to form
	a result set */
	else {
		esif_ccb_sprintf(OUT_BUF_LEN, output, "<result>\n");
		esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "  <tableRow>\n");
		for (i = 0; i < self->numFields; i++) {
			int32FieldValue = 0;
			if (self->fields[i].getPrimitive > 0) {
				response.type = self->fields[i].dataType;
				switch (self->fields[i].dataType) {
				case ESIF_DATA_STRING:
					strFieldValue = "";
					esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "    <%s>%s</%s>\n", self->fields[i].name, strFieldValue, self->fields[i].name);
					break;
				case ESIF_DATA_UINT32:
				case ESIF_DATA_POWER:
				case ESIF_DATA_TEMPERATURE:
					if (self->fields[i].dataType == ESIF_DATA_TEMPERATURE) {
						int32FieldValue = 255;
					}
					response.buf_ptr = &defaultNumber;
					response.buf_len = sizeof(defaultNumber);
					primitiveOK = EsifExecutePrimitive((UInt8) self->participantId, self->fields[i].getPrimitive, self->domainQualifier, self->fields[i].instance, &request, &response);
					if (ESIF_OK == primitiveOK) {
						int32FieldValue = *(UInt32 *) response.buf_ptr;
					}
					esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "    <%s>%u</%s>\n", self->fields[i].name, int32FieldValue, self->fields[i].name);
					break;
				case ESIF_DATA_STRUCTURE:
					response.buf_ptr = NULL;
					response.buf_len = ESIF_DATA_ALLOCATE;
					response.type = ESIF_DATA_AUTO;
					primitiveOK = EsifExecutePrimitive((UInt8) self->participantId, self->fields[i].getPrimitive, self->domainQualifier, self->fields[i].instance, &request, &response);
					if (ESIF_OK == primitiveOK && response.buf_ptr) {
						struct esif_data_binary_fst_package *fst_ptr = (struct esif_data_binary_fst_package *)response.buf_ptr;
						struct esif_data_binary_bst_package *bst_ptr = (struct esif_data_binary_bst_package *)response.buf_ptr;

						switch (self->fields[i].getPrimitive) {
						case GET_FAN_STATUS:
							esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "    <%s>%u</%s>\n", self->fields[i].name, (u32) fst_ptr->speed.integer.value, self->fields[i].name);
							break;
						case GET_BATTERY_STATUS:
							esif_ccb_sprintf_concat(OUT_BUF_LEN, output,
								" <batteryState>%u</batteryState>\n"
								" <batteryRate>%u</batteryRate>\n"
								" <batteryCapacity>%u</batteryCapacity>\n"
								" <batteryVoltage>%u</batteryVoltage>\n",
								(u32) bst_ptr->battery_state.integer.value,
								(u32) bst_ptr->battery_present_rate.integer.value,
								(u32) bst_ptr->battery_remaining_capacity.integer.value,
								(u32) bst_ptr->battery_present_voltage.integer.value);
							break;
						default:
							//do nothing
							break;
						}
					}
					esif_ccb_free(response.buf_ptr);
					break;
				default:
					ESIF_TRACE_DEBUG("Field type in schema for table %s is not handled \n", self->name);
					rc = ESIF_E_PARAMETER_IS_OUT_OF_BOUNDS;
					goto exit;
					break;
				}
			}
		}
		esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "  </tableRow>\n");
		esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "</result>\n");
	}
	self->dataXML = esif_ccb_strdup(output);
	goto exit;

exit:
	esif_ccb_free(output);
	esif_ccb_free(tmp_buf);
	EsifData_Destroy(data_nspace);
	EsifData_Destroy(data_key);
	return rc;
}

eEsifError TableObject_Convert(
	TableObject *self
	)
{
	u8 *tableMem = NULL;
	u8	*binaryOutput = NULL;
	u32 totalBytesNeeded = 0;
	u32 lengthNumber = 0;	/* For parsing IDSP only */
	u64 colValueNumber = 0;
	u64 binaryCounter = 0;  /* maintains offset for repositioning binaryOutput every realloc */
	int i = 0;
	int numFields = 0;
	char *textInput;
	EsifDataType objDataType;
	char *tableRow = NULL;
	char *rowTok = NULL;
	char *tableCol = NULL;
	char *tmp;
	char *colTok = NULL;
	char rowDelims [] = "!";
	char colDelims [] = ",";
	eEsifError rc = ESIF_OK;
	EsifDataType targetType;
	struct guid_t guid = { 0 };

	textInput = self->dataText;
	numFields = self->numFields;
	objDataType = self->dataType;
	totalBytesNeeded = 0;

	u32 stringType = ESIF_DATA_STRING;
	u32 numberType = ESIF_DATA_UINT64;
	u32 binaryType = ESIF_DATA_BINARY;
	char revisionNumString[REVISION_INDICATOR_LENGTH + 1];
	u64 revisionNumInt = 0;


	/* if the table expects a revision, the input string should be
	<revision number>:<data>, with the revision number occupying a
	char length of REVISION_INDICATOR_LENGTH */
	if (FLAGS_TEST(self->options, TABLEOPT_CONTAINS_REVISION) && esif_ccb_strlen(textInput, REVISION_INDICATOR_LENGTH + 1) > REVISION_INDICATOR_LENGTH) {
		esif_ccb_memcpy(&revisionNumString, textInput, REVISION_INDICATOR_LENGTH);
		revisionNumString[REVISION_INDICATOR_LENGTH] = '\0';
		textInput += REVISION_INDICATOR_LENGTH + 1;
		revisionNumInt = esif_atoi(revisionNumString);
		totalBytesNeeded += sizeof(numberType) + sizeof(revisionNumInt);
		tableMem = (u8*) esif_ccb_realloc(tableMem, totalBytesNeeded);
		if (tableMem == NULL) {
			rc = ESIF_E_NO_MEMORY;
			goto exit;
		}
		binaryOutput = tableMem;
		esif_ccb_memcpy(binaryOutput, &numberType, sizeof(numberType));
		binaryOutput += sizeof(numberType);
		binaryCounter += sizeof(numberType);
		esif_ccb_memcpy(binaryOutput, &revisionNumInt, sizeof(revisionNumInt));
		binaryOutput += sizeof(revisionNumInt);
		binaryCounter += sizeof(revisionNumInt);
	}

	tableRow = esif_ccb_strtok(textInput, rowDelims, &rowTok);

	while (tableRow != NULL) {
		i = -1;
		tableCol = esif_ccb_strtok(tableRow, colDelims, &colTok);
		while (tableCol != NULL) {
			u64 colValueLen = esif_ccb_strlen(tableCol, COLUMN_MAX_SIZE) + 1;

			i++;
			if (i < numFields) {
				if ((tmp = esif_ccb_strstr(tableCol, "'")) != NULL) {
					*tmp = 0;
				}

				if (FLAGS_TEST(self->options, TABLEOPT_ALLOW_SELF_DEFINE)) {
					targetType = (esif_atoi(tableCol) > 0 || strcmp(tableCol, "0") == 0) ? ESIF_DATA_UINT64 : ESIF_DATA_STRING;
				}
				else {
					targetType = self->fields[i].dataType;
				}
				switch (targetType) {
				case ESIF_DATA_STRING:
					totalBytesNeeded += sizeof(stringType) + (u32)sizeof(colValueLen) + (u32) colValueLen;
					tableMem = (u8*) esif_ccb_realloc(tableMem, totalBytesNeeded);
					if (tableMem == NULL) {
						rc = ESIF_E_NO_MEMORY;
						goto exit;
					}
					binaryOutput = tableMem;
					binaryOutput += binaryCounter;
					esif_ccb_memcpy(binaryOutput, &stringType, sizeof(stringType));
					binaryOutput += sizeof(stringType);
					binaryCounter += sizeof(stringType);
					esif_ccb_memcpy(binaryOutput, &colValueLen, sizeof(colValueLen));
					binaryOutput += sizeof(colValueLen);
					binaryCounter += sizeof(colValueLen);
					esif_ccb_memcpy(binaryOutput, tableCol, (size_t) colValueLen);
					binaryOutput += colValueLen;
					binaryCounter += colValueLen;
					break;
				case ESIF_DATA_UINT32:	// UInt32 and UInt64 treated equally because bios field is always 64 for number
				case ESIF_DATA_UINT64:
					colValueNumber = esif_atoi(tableCol);
					totalBytesNeeded += sizeof(numberType) + (u32)sizeof(colValueNumber);
					tableMem = (u8*) esif_ccb_realloc(tableMem, totalBytesNeeded);
					if (tableMem == NULL) {
						rc = ESIF_E_NO_MEMORY;
						goto exit;
					}
					binaryOutput = tableMem;
					binaryOutput += binaryCounter;
					esif_ccb_memcpy(binaryOutput, &numberType, sizeof(numberType));
					binaryOutput += sizeof(numberType);
					binaryCounter += sizeof(numberType);
					esif_ccb_memcpy(binaryOutput, &colValueNumber, sizeof(colValueNumber));
					binaryOutput += sizeof(colValueNumber);
					binaryCounter += sizeof(colValueNumber);
					break;
				case ESIF_DATA_BINARY:
					lengthNumber = esif_atoi(tableCol);  // First field is length of binary data
					u32 reserved = 0;                      // Binary Type implies a 4-byte reserved field after length
					totalBytesNeeded += sizeof(binaryType) + sizeof(lengthNumber) + sizeof(reserved) + lengthNumber;
					tableMem = (u8*) esif_ccb_realloc(tableMem, totalBytesNeeded);
					if (tableMem == NULL) {
						rc = ESIF_E_NO_MEMORY;
						goto exit;
					}
					binaryOutput = tableMem + binaryCounter;
					esif_ccb_memcpy(binaryOutput, &binaryType, sizeof(binaryType));  // Type
					binaryOutput += sizeof(binaryType);
					esif_ccb_memcpy(binaryOutput, &lengthNumber, sizeof(lengthNumber));  // Length
					binaryOutput += sizeof(lengthNumber);
					esif_ccb_memcpy(binaryOutput, &reserved, sizeof(reserved));  // Reserved
					binaryOutput += sizeof(reserved);
					// Must get the next column here (the UUID) because it is part of a structure
					tableCol = esif_ccb_strtok(NULL, colDelims, &colTok);
					
					if (tableCol) {
						esif_ccb_sscanf(tableCol, "%8x-%4hx-%4hx-%2hhx%2hhx-%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx", 
						&guid.data1, &guid.data2, &guid.data3, 
						&guid.data4[0], &guid.data4[1], &guid.data4[2], &guid.data4[3],
						&guid.data4[4], &guid.data4[5], &guid.data4[6], &guid.data4[7]);
						esif_ccb_memcpy(binaryOutput, &guid, sizeof(guid));
						binaryOutput += sizeof(guid);
					}
					binaryCounter = binaryOutput - tableMem;

					break;
				default:
					ESIF_TRACE_DEBUG("Field type in schema for table %s is not handled \n", self->name);
					rc = ESIF_E_PARAMETER_IS_OUT_OF_BOUNDS;
					goto exit;
					break;
				}
			}

			// Get next column
			tableCol = esif_ccb_strtok(NULL, colDelims, &colTok);
		}

		// Get next row
		tableRow = esif_ccb_strtok(NULL, rowDelims, &rowTok);
	}


	if (totalBytesNeeded) {
		self->binaryData = esif_ccb_malloc(totalBytesNeeded);
		esif_ccb_memcpy((u8 *) self->binaryData, tableMem, totalBytesNeeded);
		self->binaryDataSize = totalBytesNeeded;
	}
	else {

		rc = ESIF_E_UNSPECIFIED;
	}

exit:
	esif_ccb_free(tableMem);
	return rc;
}

void TableObject_Construct(
	TableObject *self,
	char *dataName,
	char *domainQualifier,
	int participantId
	)
{
	esif_ccb_memset(self, 0, sizeof(*self));
	self->name = esif_ccb_strdup(dataName);
	self->domainQualifier = esif_ccb_strdup(domainQualifier);
	self->participantId = participantId;
}

eEsifError TableObject_Delete(
	TableObject *self
	)
{
	char *namesp = ESIF_DSP_OVERRIDE_NAMESPACE;
	eEsifError rc = ESIF_OK;
	EsifDataPtr  data_nspace = NULL;
	EsifDataPtr  data_key = NULL;
	EsifDataPtr  data_value = NULL;
	esif_flags_t options = ESIF_SERVICE_CONFIG_DELETE;
	struct esif_data voidData = { ESIF_DATA_VOID, NULL, 0 };
	UInt16 domain = EVENT_MGR_DOMAIN_D0;
	EsifDataType objDataType = ESIF_DATA_BINARY;
	EsifUpPtr upPtr = NULL;

	ESIF_ASSERT(self != NULL);

	objDataType = self->dataType;

	if (esif_ccb_strlen(self->domainQualifier, sizeof(UInt16)) < sizeof(UInt16)) {
		rc = ESIF_E_PARAMETER_IS_NULL;
		goto exit;
	}
	domain = domain_str_to_short(self->domainQualifier);

	/* 
	 * First determine if this is a binary table 
	 * or a virtual table 
	 */
	if (objDataType == ESIF_DATA_BINARY) {

		if (self->dataVaultKey == NULL) {
			rc = ESIF_E_NOT_IMPLEMENTED;
			goto exit;
		}

		data_nspace = EsifData_CreateAs(ESIF_DATA_STRING, namesp, 0, ESIFAUTOLEN);
		data_key = EsifData_CreateAs(ESIF_DATA_STRING, self->dataVaultKey, 0, ESIFAUTOLEN);
		data_value = EsifData_Create();

		if (data_nspace == NULL || data_key == NULL || data_value == NULL) {
			rc = ESIF_E_NO_MEMORY;
			goto exit;
		}

		TableObject_NoPersist(self, (char *)data_nspace->buf_ptr, (char *)data_key->buf_ptr);
		rc = EsifConfigSet(data_nspace, data_key, options, data_value);
		/* 
		 * Send event regardless of rc code to allow flexibility in the return value, and
		 * no harm is done by the event 
		 */
		EsifEventMgr_SignalEvent((UInt8) self->participantId, domain, self->changeEvent, &voidData);
	}
	else {  //virtual tables
		int i = 0;
		upPtr = EsifUpPm_GetAvailableParticipantByInstance((UInt8) self->participantId);
		if (NULL == upPtr) {
			rc = ESIF_E_PARTICIPANT_NOT_FOUND;
			goto exit;
		}
		data_nspace = EsifData_CreateAs(ESIF_DATA_STRING, namesp, 0, ESIFAUTOLEN);
		data_value = EsifData_Create();
		if (data_nspace == NULL || data_value == NULL) {
			rc = ESIF_E_NO_MEMORY;
			goto exit;
		}
		for (i = 0; i < self->numFields; i++) {
			char targetKey[MAX_TABLEOBJECT_KEY_LEN] = { 0 };
			EsifDataPtr targetDataKey = NULL;
			
			if (self->fields[i].dataVaultKey != NULL) {
				esif_ccb_strcpy(targetKey, self->fields[i].dataVaultKey, MAX_TABLEOBJECT_KEY_LEN);
			}
			else if (self->dataVaultCategory == NULL) {
				esif_ccb_sprintf(MAX_TABLEOBJECT_KEY_LEN, targetKey,
					"/participants/%s.%s/%.*s%s",
					EsifUp_GetName(upPtr), self->domainQualifier,
					(int) (ACPI_NAME_TARGET_SIZE - esif_ccb_min(esif_ccb_strlen(self->name, ACPI_NAME_TARGET_SIZE), ACPI_NAME_TARGET_SIZE)), "____",
					self->fields[i].name);
			}
			else {
				esif_ccb_sprintf(MAX_TABLEOBJECT_KEY_LEN, targetKey,
					"/participants/%s.%s/%s/%.*s%s",
					EsifUp_GetName(upPtr), self->domainQualifier,self->dataVaultCategory,
					(int) (ACPI_NAME_TARGET_SIZE - esif_ccb_min(esif_ccb_strlen(self->name, ACPI_NAME_TARGET_SIZE), ACPI_NAME_TARGET_SIZE)), "____",
					self->fields[i].name);
			}

			targetDataKey = EsifData_CreateAs(ESIF_DATA_STRING, targetKey, 0, ESIFAUTOLEN);

			if (targetDataKey == NULL) {
				continue;
			}

			/* 
			 * Best effort depending on the fact that there 
			 * are field keys in the datavault
			 */
			TableObject_NoPersist(self, (char *)data_nspace->buf_ptr, (char *)targetDataKey->buf_ptr);
			rc = EsifConfigSet(data_nspace, targetDataKey, options, data_value);
			EsifData_Destroy(targetDataKey);
		}
	}

exit:
	if (upPtr != NULL) {
		EsifUp_PutRef(upPtr);
	}
	EsifData_Destroy(data_nspace);
	EsifData_Destroy(data_key);
	EsifData_Destroy(data_value);
	return rc;
}

eEsifError TableObject_LoadSchema(
	TableObject *self
	)
{
	eEsifError rc = ESIF_OK;
	EsifUpPtr upPtr = NULL;
	char targetKey[MAX_TABLEOBJECT_KEY_LEN] = { 0 };
	TableField *fieldlist = NULL;

	upPtr = EsifUpPm_GetAvailableParticipantByInstance((UInt8) self->participantId);
	if (NULL == upPtr) {
		rc = ESIF_E_PARTICIPANT_NOT_FOUND;
		goto exit;
	}

	/* replace with lookup */
	if (esif_ccb_stricmp(self->name, "trt") == 0) {
		static TableField trt_fields [] = {
				{ "src",		"source",		ESIF_DATA_STRING },
				{ "dst",		"destination",	ESIF_DATA_STRING },
				{ "priority",	"influence",	ESIF_DATA_UINT64 },
				{ "sampleRate",	"period",		ESIF_DATA_UINT64 },
				{ "reserved0",	"rsvd_0",		ESIF_DATA_UINT64 },
				{ "reserved1",	"rsvd_1",		ESIF_DATA_UINT64 },
				{ "reserved2",	"rsvd_2",		ESIF_DATA_UINT64 },
				{ "reserved3",	"rsvd_3",		ESIF_DATA_UINT64 },
				{ 0 }
		};
		self->dataType = ESIF_DATA_BINARY;
		self->getPrimitive = GET_THERMAL_RELATIONSHIP_TABLE;
		self->setPrimitive = SET_THERMAL_RELATIONSHIP_TABLE;
		self->changeEvent = ESIF_EVENT_APP_THERMAL_RELATIONSHIP_CHANGED;
		fieldlist = trt_fields;
	}
	else if (esif_ccb_stricmp(self->name, "art") == 0) {
		static TableField art_fields [] = {
				{ "src",		"source",		ESIF_DATA_STRING },
				{ "dst",		"destination",	ESIF_DATA_STRING },
				{ "priority",	"influence",	ESIF_DATA_UINT64 },
				{ "ac0",		"ac0",			ESIF_DATA_UINT64 },
				{ "ac1",		"ac1",			ESIF_DATA_UINT64 },
				{ "ac2",		"ac2",			ESIF_DATA_UINT64 },
				{ "ac3",		"ac3",			ESIF_DATA_UINT64 },
				{ "ac4",		"ac4",			ESIF_DATA_UINT64 },
				{ "ac5",		"ac5",			ESIF_DATA_UINT64 },
				{ "ac6",		"ac6",			ESIF_DATA_UINT64 },
				{ "ac7",		"ac7",			ESIF_DATA_UINT64 },
				{ "ac8",		"ac8",			ESIF_DATA_UINT64 },
				{ "ac9",		"ac9",			ESIF_DATA_UINT64 },
				{ 0 }
		};
		FLAGS_SET(self->options, TABLEOPT_CONTAINS_REVISION);
		FLAGS_CLEAR(self->options, TABLEOPT_ALLOW_SELF_DEFINE);
		self->dataType = ESIF_DATA_BINARY;
		self->getPrimitive = GET_ACTIVE_RELATIONSHIP_TABLE;
		self->setPrimitive = SET_ACTIVE_RELATIONSHIP_TABLE;
		self->changeEvent = ESIF_EVENT_APP_ACTIVE_RELATIONSHIP_CHANGED;
		fieldlist = art_fields;
	}
	else if (esif_ccb_stricmp(self->name, "psvt") == 0) {
		static TableField psvt_fields [] = {
				{ "fld1", "fld1", ESIF_DATA_STRING, 1 },
				{ "fld2", "fld2", ESIF_DATA_STRING },
				{ "fld3", "fld3", ESIF_DATA_UINT64 },
				{ "fld4", "fld4", ESIF_DATA_UINT64 },
				{ "fld5", "fld5", ESIF_DATA_UINT64 },
				{ "fld6", "fld6", ESIF_DATA_UINT64 },
				{ "fld7", "fld7", ESIF_DATA_UINT64 },
				{ "fld8", "fld8", ESIF_DATA_STRING },
				{ "fld9", "fld9", ESIF_DATA_UINT64 },
				{ "fld10","fld10", ESIF_DATA_UINT64 },
				{ "fld11","fld11", ESIF_DATA_UINT64 },
				{ "fld12","fld12", ESIF_DATA_UINT64 },
				{ 0 }
		};
		FLAGS_SET(self->options, TABLEOPT_CONTAINS_REVISION);
		FLAGS_SET(self->options, TABLEOPT_ALLOW_SELF_DEFINE);
		FLAGS_SET(self->options, TABLEOPT_HAS_NOPERSIST);
		self->dataType = ESIF_DATA_BINARY;
		self->getPrimitive = GET_PASSIVE_RELATIONSHIP_TABLE;
		self->setPrimitive = SET_PASSIVE_RELATIONSHIP_TABLE;
		self->changeEvent = ESIF_EVENT_PASSIVE_TABLE_CHANGED;
		fieldlist = psvt_fields;
	}
	else if (esif_ccb_stricmp(self->name, "apct") == 0) {
		static TableField apct_fields [] = {
				{ "fld1", "fld1", ESIF_DATA_UINT64 },
				{ "fld2", "fld2", ESIF_DATA_UINT64 },
				{ "fld3", "fld3", ESIF_DATA_UINT64 },
				{ "fld4", "fld4", ESIF_DATA_UINT64 },
				{ "fld5", "fld5", ESIF_DATA_UINT64 },
				{ "fld6", "fld6", ESIF_DATA_UINT64 },
				{ "fld7", "fld7", ESIF_DATA_UINT64 },
				{ "fld8", "fld8", ESIF_DATA_UINT64 },
				{ "fld9", "fld9", ESIF_DATA_UINT64 },
				{ "fld10", "fld10", ESIF_DATA_UINT64 },
				{ "fld11", "fld11", ESIF_DATA_UINT64 },
				{ "fld12", "fld12", ESIF_DATA_UINT64 },
				{ "fld13", "fld13", ESIF_DATA_UINT64 },
				{ "fld14", "fld14", ESIF_DATA_UINT64 },
				{ "fld15", "fld15", ESIF_DATA_UINT64 },
				{ "fld16", "fld16", ESIF_DATA_UINT64 },
				{ "fld17", "fld17", ESIF_DATA_UINT64 },
				{ "fld18", "fld18", ESIF_DATA_UINT64 },
				{ "fld19", "fld19", ESIF_DATA_UINT64 },
				{ "fld20", "fld20", ESIF_DATA_UINT64 },
				{ "fld21", "fld21", ESIF_DATA_UINT64 },
				{ "fld22", "fld22", ESIF_DATA_UINT64 },
				{ "fld23", "fld23", ESIF_DATA_UINT64 },
				{ "fld24", "fld24", ESIF_DATA_UINT64 },
				{ "fld25", "fld25", ESIF_DATA_UINT64 },
				{ "fld26", "fld26", ESIF_DATA_UINT64 },
				{ "fld27", "fld27", ESIF_DATA_UINT64 },
				{ "fld28", "fld28", ESIF_DATA_UINT64 },
				{ "fld29", "fld29", ESIF_DATA_UINT64 },
				{ "fld30", "fld30", ESIF_DATA_UINT64 },
				{ "fld31", "fld31", ESIF_DATA_UINT64 },
				{ "fld32", "fld32", ESIF_DATA_UINT64 },
				{ "fld33", "fld33", ESIF_DATA_UINT64 },
				{ "fld34", "fld34", ESIF_DATA_UINT64 },
				{ "fld35", "fld35", ESIF_DATA_UINT64 },
				{ "fld36", "fld36", ESIF_DATA_UINT64 },
				{ "fld37", "fld37", ESIF_DATA_UINT64 },
				{ "fld38", "fld38", ESIF_DATA_UINT64 },
				{ "fld39", "fld39", ESIF_DATA_UINT64 },
				{ "fld40", "fld40", ESIF_DATA_UINT64 },
				{ 0 }
		};
		FLAGS_SET(self->options, TABLEOPT_CONTAINS_REVISION);
		FLAGS_SET(self->options, TABLEOPT_ALLOW_SELF_DEFINE);
		self->dataType = ESIF_DATA_BINARY;
		self->getPrimitive = GET_ADAPTIVE_PERFORMANCE_CONDITIONS_TABLE;
		self->setPrimitive = SET_ADAPTIVE_PERFORMANCE_CONDITIONS_TABLE;
		self->changeEvent = ESIF_EVENT_ADAPTIVE_PERFORMANCE_CONDITIONS_CHANGED;
		fieldlist = apct_fields;
	}
    else if (esif_ccb_stricmp(self->name, "apat") == 0) {
        static TableField apat_fields[] = {
				{ "fld1",	"fld1",	ESIF_DATA_UINT64 },
				{ "fld2",	"fld2",	ESIF_DATA_STRING },
				{ "fld3",	"fld3",			ESIF_DATA_STRING },
				{ "fld4",	"fld4",		ESIF_DATA_STRING },
            	{ 0 }
        };
		FLAGS_SET(self->options, TABLEOPT_CONTAINS_REVISION);
		FLAGS_CLEAR(self->options, TABLEOPT_ALLOW_SELF_DEFINE);
		self->dataType = ESIF_DATA_BINARY;
        self->getPrimitive = GET_ADAPTIVE_PERFORMANCE_ACTIONS_TABLE;
        self->setPrimitive = SET_ADAPTIVE_PERFORMANCE_ACTIONS_TABLE;
        self->changeEvent = ESIF_EVENT_ADAPTIVE_PERFORMANCE_ACTIONS_CHANGED;
        fieldlist = apat_fields;
    }
	else if (esif_ccb_stricmp(self->name, "idsp") == 0) {
		static TableField idsp_fields [] = {
				{ "uuid", "uuid", ESIF_DATA_UINT64 },
				{ 0 }
		};
		FLAGS_CLEAR(self->options, TABLEOPT_CONTAINS_REVISION);
		FLAGS_SET(self->options, TABLEOPT_ALLOW_SELF_DEFINE);
		self->dataType = ESIF_DATA_BINARY;
		self->getPrimitive = GET_SUPPORTED_POLICIES;
		self->setPrimitive = 0;
		self->changeEvent = ESIF_EVENT_PARTICIPANT_SPEC_INFO_CHANGED;
		fieldlist = idsp_fields;
		
	}
	else if (esif_ccb_stricmp(self->name, "ppcc") == 0) {
		static TableField ppcc_fields [] = {
				{ "revision",	"revision",		ESIF_DATA_UINT64 },
				{ "PL1Index",	"PL1Index",		ESIF_DATA_UINT64 },
				{ "PL1Min",		"PL1Min",		ESIF_DATA_UINT64 },
				{ "PL1Max",		"PL1Max",		ESIF_DATA_UINT64 },
				{ "PL1TimeMin", "PL1TimeMin",	ESIF_DATA_UINT64 },
				{ "PL1TimeMax",	"PL1TimeMax",	ESIF_DATA_UINT64 },
				{ "PL1Step",	"PL1Step",		ESIF_DATA_UINT64 },
				{ "PL2Index",	"PL2Index",		ESIF_DATA_UINT64 },
				{ "PL2Min",		"PL2Min",		ESIF_DATA_UINT64 },
				{ "PL2Max",		"PL2Max",		ESIF_DATA_UINT64 },
				{ "PL2TimeMin",	"PL2TimeMin",	ESIF_DATA_UINT64 },
				{ "PL2TimeMax", "PL2TimeMax",	ESIF_DATA_UINT64 },
				{ "PL2Step",	"PL2Step",		ESIF_DATA_UINT64 },
				{ 0 }
		};
		self->dataType = ESIF_DATA_BINARY;
		self->getPrimitive = GET_RAPL_POWER_CONTROL_CAPABILITIES;
		self->setPrimitive = SET_RAPL_POWER_CONTROL_CAPABILITIES;
		self->changeEvent = ESIF_EVENT_DOMAIN_POWER_CAPABILITY_CHANGED;
		fieldlist = ppcc_fields;
	}
	else if (esif_ccb_stricmp(self->name, "vsct") == 0) {
		static TableField vsct_fields [] = {
				{ "fld1", "fld1", ESIF_DATA_STRING },
				{ "fld2", "fld2", ESIF_DATA_UINT64 },
				{ "fld3", "fld3", ESIF_DATA_UINT64 },
				{ "fld4", "fld4", ESIF_DATA_UINT64 },
				{ "fld5", "fld5", ESIF_DATA_UINT64 },
				{ "fld6", "fld6", ESIF_DATA_UINT64 },
				{ "fld7", "fld7", ESIF_DATA_UINT64 },
				{ 0 }
		};
		FLAGS_SET(self->options, TABLEOPT_CONTAINS_REVISION);
		FLAGS_CLEAR(self->options, TABLEOPT_ALLOW_SELF_DEFINE);
		self->dataType = ESIF_DATA_BINARY;
		self->getPrimitive = GET_VIRTUAL_SENSOR_CALIB_TABLE;
		self->setPrimitive = SET_VIRTUAL_SENSOR_CALIB_TABLE;
		self->changeEvent = ESIF_EVENT_VIRTUAL_SENSOR_CALIB_TABLE_CHANGED;
		fieldlist = vsct_fields;
	}
	else if (esif_ccb_stricmp(self->name, "vspt") == 0) {
		static TableField vspt_fields [] = {
				{ "fld1", "fld1", ESIF_DATA_UINT64 },
				{ "fld2", "fld2", ESIF_DATA_UINT64 },
				{ 0 }
		};
		FLAGS_SET(self->options, TABLEOPT_CONTAINS_REVISION);
		FLAGS_CLEAR(self->options, TABLEOPT_ALLOW_SELF_DEFINE);
		self->dataType = ESIF_DATA_BINARY;
		self->getPrimitive = GET_VIRTUAL_SENSOR_POLLING_TABLE;
		self->setPrimitive = SET_VIRTUAL_SENSOR_POLLING_TABLE;
		self->changeEvent = ESIF_EVENT_VIRTUAL_SENSOR_POLLING_TABLE_CHANGED;
		fieldlist = vspt_fields;
	}
	else if (esif_ccb_stricmp(self->name, "ppss") == 0) {
		static TableField ppss_fields [] = {
				{ "Performance",		"Performance",			ESIF_DATA_UINT64 },
				{ "Power",				"Power",				ESIF_DATA_UINT64 },
				{ "TransitionLatency",	"TransitionLatency",	ESIF_DATA_UINT64 },
				{ "Linear",				"Linear",				ESIF_DATA_UINT64 },
				{ "Control",			"Control",				ESIF_DATA_UINT64 },
				{ "RawPerformance",		"RawPerformance",		ESIF_DATA_UINT64 },
				{ "RawUnit",			"RawUnit",				ESIF_DATA_STRING },
				{ "Reserved1",			"Reserved1",			ESIF_DATA_UINT64 },
				{ 0 }
		};
		self->dataType = ESIF_DATA_BINARY;
		self->getPrimitive = GET_PERF_SUPPORT_STATES;
		self->setPrimitive = SET_PERF_SUPPORT_STATE;
		self->changeEvent = ESIF_EVENT_DOMAIN_PERF_CONTROL_CHANGED;
		fieldlist = ppss_fields;
	}
	else if (esif_ccb_stricmp(self->name, "pss") == 0) {
		static TableField pss_fields [] = {
				{ "ControlValue",	"ControlValue",	ESIF_DATA_UINT64 },
				{ "TDPPower",		"TDPPower",		ESIF_DATA_UINT64 },
				{ "Latency1",		"Latency1",		ESIF_DATA_UINT64 },
				{ "Latency2",		"Latency2",		ESIF_DATA_UINT64 },
				{ "ControlID1",		"ControlID1",	ESIF_DATA_UINT64 },
				{ "ControlID2",		"ControlID2",	ESIF_DATA_UINT64 },
				{ 0 }
		};
		self->dataType = ESIF_DATA_BINARY;
		self->getPrimitive = GET_PROC_PERF_SUPPORT_STATES;
		self->setPrimitive = 0;
		self->changeEvent = ESIF_EVENT_DOMAIN_PERF_CONTROL_CHANGED;
		fieldlist = pss_fields;
	}
	else if (esif_ccb_stricmp(self->name, "psv") == 0) {
		static TableField psv_fields [] = {
				{ "psv",	"psv",		ESIF_DATA_TEMPERATURE,	GET_TRIP_POINT_PASSIVE,					SET_TRIP_POINT_PASSIVE,					255 },
				{ "cr3",	"cr3",		ESIF_DATA_TEMPERATURE,	GET_TRIP_POINT_WARM,					SET_TRIP_POINT_WARM,					255 },
				{ "hot",	"hot",		ESIF_DATA_TEMPERATURE,	GET_TRIP_POINT_HOT,						SET_TRIP_POINT_HOT,						255 },
				{ "crt",	"crt",		ESIF_DATA_TEMPERATURE,	GET_TRIP_POINT_CRITICAL,				SET_TRIP_POINT_CRITICAL,				255 },
				{ "ac0",	"ac0",		ESIF_DATA_TEMPERATURE,	GET_TRIP_POINT_ACTIVE,					SET_TRIP_POINT_ACTIVE,					0 },
				{ "ac1",	"ac1",		ESIF_DATA_TEMPERATURE,	GET_TRIP_POINT_ACTIVE,					SET_TRIP_POINT_ACTIVE,					1 },
				{ "ac2",	"ac2",		ESIF_DATA_TEMPERATURE,	GET_TRIP_POINT_ACTIVE,					SET_TRIP_POINT_ACTIVE,					2 },
				{ "ac3",	"ac3",		ESIF_DATA_TEMPERATURE,	GET_TRIP_POINT_ACTIVE,					SET_TRIP_POINT_ACTIVE,					3 },
				{ "ac4",	"ac4",		ESIF_DATA_TEMPERATURE,	GET_TRIP_POINT_ACTIVE,					SET_TRIP_POINT_ACTIVE,					4 },
				{ "ac5",	"ac5",		ESIF_DATA_TEMPERATURE,	GET_TRIP_POINT_ACTIVE,					SET_TRIP_POINT_ACTIVE,					5 },
				{ "ac6",	"ac6",		ESIF_DATA_TEMPERATURE,	GET_TRIP_POINT_ACTIVE,					SET_TRIP_POINT_ACTIVE,					6 },
				{ "ac7",	"ac7",		ESIF_DATA_TEMPERATURE,	GET_TRIP_POINT_ACTIVE,					SET_TRIP_POINT_ACTIVE,					7 },
				{ "ac8",	"ac8",		ESIF_DATA_TEMPERATURE,	GET_TRIP_POINT_ACTIVE,					SET_TRIP_POINT_ACTIVE,					8 },
				{ "ac9",	"ac9",		ESIF_DATA_TEMPERATURE,	GET_TRIP_POINT_ACTIVE,					SET_TRIP_POINT_ACTIVE,					9 },
				{ "hyst",	"hyst",		ESIF_DATA_TEMPERATURE,	GET_TEMPERATURE_THRESHOLD_HYSTERESIS,	SET_TEMPERATURE_THRESHOLD_HYSTERESIS,	255 },
				{ 0 }
		};
		self->dataType = ESIF_DATA_UINT32;
		self->getPrimitive = 0;  /* NA for virtual tables */
		self->setPrimitive = 0;
		self->changeEvent = 0;
		self->dataVaultCategory = esif_ccb_strdup("trippoint");
		fieldlist = psv_fields;
	}
	else if (esif_ccb_stricmp(self->name, "status") == 0) {
		static TableField status_fields [] = {
				{ "temp",				"temp",				ESIF_DATA_TEMPERATURE,	GET_TEMPERATURE,						SET_TEMPERATURE,						255 },
				{ "power",				"power",			ESIF_DATA_POWER,		GET_RAPL_POWER,							0,										255 },
				{ "fanSpeed",			"fanSpeed",			ESIF_DATA_STRUCTURE,	GET_FAN_STATUS,							0,										255 },
				{ "battery",			"battery",			ESIF_DATA_STRUCTURE,	GET_BATTERY_STATUS,						0,										255 },
				{ "platform",			"platform",			ESIF_DATA_TEMPERATURE,	GET_TRIP_POINT_CRITICAL,				SET_TRIP_POINT_CRITICAL,				255 },
				{ "wrm",				"wrm",				ESIF_DATA_TEMPERATURE,	GET_TRIP_POINT_WARM,					SET_TRIP_POINT_WARM,					255 },
				{ "hot",				"hot",				ESIF_DATA_TEMPERATURE,	GET_TRIP_POINT_HOT,						SET_TRIP_POINT_HOT,						255 },
				{ "crt",				"crt",				ESIF_DATA_TEMPERATURE,	GET_TRIP_POINT_CRITICAL,				SET_TRIP_POINT_CRITICAL,				255 },
				{ "psv",				"psv",				ESIF_DATA_TEMPERATURE,	GET_TRIP_POINT_PASSIVE,					SET_TRIP_POINT_PASSIVE,					255 },
				{ "tempAux0",			"tempAux0",			ESIF_DATA_TEMPERATURE,	GET_TEMPERATURE_THRESHOLDS,				SET_TEMPERATURE_THRESHOLDS,				255 },
				{ "tempAux1",			"tempAux1",			ESIF_DATA_TEMPERATURE,	GET_TEMPERATURE_THRESHOLDS,				SET_TEMPERATURE_THRESHOLDS,				255 },
				{ "tempHyst",			"tempHyst",			ESIF_DATA_TEMPERATURE,	GET_TEMPERATURE_THRESHOLD_HYSTERESIS,	SET_TEMPERATURE_THRESHOLD_HYSTERESIS,	255 },
				{ "powerAux0",			"powerAux0",		ESIF_DATA_TEMPERATURE,	GET_TRIP_POINT_CRITICAL,				SET_TRIP_POINT_CRITICAL,				255 },
				{ "powerAux1",			"powerAux1",		ESIF_DATA_TEMPERATURE,	GET_TRIP_POINT_CRITICAL,				SET_TRIP_POINT_CRITICAL,				255 },
				{ "ac0",				"ac0",				ESIF_DATA_TEMPERATURE,	GET_TRIP_POINT_ACTIVE,					SET_TRIP_POINT_ACTIVE,					0 },
				{ "ac1",				"ac1",				ESIF_DATA_TEMPERATURE,	GET_TRIP_POINT_ACTIVE,					SET_TRIP_POINT_ACTIVE,					1 },
				{ "ac2",				"ac2",				ESIF_DATA_TEMPERATURE,	GET_TRIP_POINT_ACTIVE,					SET_TRIP_POINT_ACTIVE,					2 },
				{ "ac3",				"ac3",				ESIF_DATA_TEMPERATURE,	GET_TRIP_POINT_ACTIVE,					SET_TRIP_POINT_ACTIVE,					3 },
				{ "ac4",				"ac4",				ESIF_DATA_TEMPERATURE,	GET_TRIP_POINT_ACTIVE,					SET_TRIP_POINT_ACTIVE,					4 },
				{ "ac5",				"ac5",				ESIF_DATA_TEMPERATURE,	GET_TRIP_POINT_ACTIVE,					SET_TRIP_POINT_ACTIVE,					5 },
				{ "ac6",				"ac6",				ESIF_DATA_TEMPERATURE,	GET_TRIP_POINT_ACTIVE,					SET_TRIP_POINT_ACTIVE,					6 },
				{ "ac7",				"ac7",				ESIF_DATA_TEMPERATURE,	GET_TRIP_POINT_ACTIVE,					SET_TRIP_POINT_ACTIVE,					6 },
				{ "ac8",				"ac8",				ESIF_DATA_TEMPERATURE,	GET_TRIP_POINT_ACTIVE,					SET_TRIP_POINT_ACTIVE,					8 },
				{ "ac9",				"ac9",				ESIF_DATA_TEMPERATURE,	GET_TRIP_POINT_ACTIVE,					SET_TRIP_POINT_ACTIVE,					9 },
				{ "activeCoreCount",	"activeCoreCount",	ESIF_DATA_UINT32,		GET_PROC_LOGICAL_PROCESSOR_COUNT,		0,										255 },
				{ "pStateLimit",		"pStateLimit",		ESIF_DATA_UINT32,		GET_PROC_PERF_PSTATE_DEPTH_LIMIT,		SET_PROC_PERF_PSTATE_DEPTH_LIMIT,		255 },
				{ "powerLimit1",		"powerLimit1",		ESIF_DATA_POWER,		GET_RAPL_POWER_LIMIT,					SET_RAPL_POWER_LIMIT,					0 },
				{ "powerLimit2",		"powerLimit2",		ESIF_DATA_POWER,		GET_RAPL_POWER_LIMIT,					SET_RAPL_POWER_LIMIT,					1 },
				{ "samplePeriod",		"samplePeriod",		ESIF_DATA_UINT32,		GET_PARTICIPANT_SAMPLE_PERIOD,			SET_PARTICIPANT_SAMPLE_PERIOD,			255 },
				{ 0 }
		};
		self->dataType = ESIF_DATA_UINT32;
		self->getPrimitive = 0;  /* NA for virtual tables */
		self->setPrimitive = 0;
		self->changeEvent = 0;
		fieldlist = status_fields;
	}
	else if (esif_ccb_stricmp(self->name, "workload") == 0) {
		static TableField workload_fields [] = {
				{ "workload", "workload", ESIF_DATA_STRING, 0, 0, 255, "policies/B7-F1-CA-16-38-DD-ED-40-B1-C1-1B-8A-19-13-D5-31/workloads"},
				{ 0 }
		};
		self->dataType = ESIF_DATA_STRING;
		self->getPrimitive = 0;  /* NA for virtual tables */
		self->setPrimitive = 0;
		self->changeEvent = 0;
		self->dataVaultKey = esif_ccb_strdup("policies/B7-F1-CA-16-38-DD-ED-40-B1-C1-1B-8A-19-13-D5-31/workloads/*");
		fieldlist = workload_fields;
	}
	else if (esif_ccb_stricmp(self->name, "participant_min") == 0) {
		static TableField pmin_fields [] = {
				{ "temp",	"temp",		ESIF_DATA_TEMPERATURE,	GET_TEMPERATURE,	SET_TEMPERATURE,	255 },
				{ "power",	"power",	ESIF_DATA_POWER,		GET_RAPL_POWER,		0,					255 },
				{ 0 }
		};
		self->dataType = ESIF_DATA_UINT32;
		self->getPrimitive = 0;  /* NA for virtual tables */
		self->setPrimitive = 0;
		self->changeEvent = 0;
		fieldlist = pmin_fields;
	}
	else if (esif_ccb_stricmp(self->name, "pdl") == 0) {
		static TableField pdl_fields [] = {
				{ "value","value", ESIF_DATA_UINT32, GET_PROC_PERF_PSTATE_DEPTH_LIMIT, SET_PROC_PERF_PSTATE_DEPTH_LIMIT, 255 },
				{ 0 }
		};
		self->dataType = ESIF_DATA_UINT32;
		self->getPrimitive = 0;  /* NA for virtual tables */
		self->setPrimitive = 0;
		self->changeEvent = 0;
		fieldlist = pdl_fields;
	}
	else if (esif_ccb_stricmp(self->name, "pdl_participant") == 0) {
		static TableField pdl_participant_fields [] = {
				{ "value", "value", ESIF_DATA_UINT32, GET_PERF_PSTATE_DEPTH_LIMIT, 0, 255 },
				{ 0 }
		};
		self->dataType = ESIF_DATA_UINT32;
		self->getPrimitive = 0;  /* NA for virtual tables */
		self->setPrimitive = 0;
		self->changeEvent = 0;
		fieldlist = pdl_participant_fields;
	}
	else {
		rc = ESIF_E_NOT_IMPLEMENTED;
	}

	/* Construct Field List and DataVault Key */
	if (fieldlist != NULL) {
		int j = 0;
		if (self->dataType == ESIF_DATA_BINARY) {
			for (j = 0; fieldlist[j].name != NULL; j++) {
				TableField_Construct(&(self->fields[j]), fieldlist[j].name, fieldlist[j].label, fieldlist[j].dataType, fieldlist[j].notForXML);
			}
		}
		else {   /* virtual tables */
			for (j = 0; fieldlist[j].name != NULL; j++) {
				TableField_ConstructAs(&(self->fields[j]), fieldlist[j].name, fieldlist[j].label, fieldlist[j].dataType, fieldlist[j].notForXML, fieldlist[j].getPrimitive, fieldlist[j].setPrimitive, fieldlist[j].instance, fieldlist[j].dataVaultKey);
			}
		}
		self->numFields = j;

		if (self->dataVaultKey == NULL) {
			esif_ccb_sprintf(MAX_TABLEOBJECT_KEY_LEN, targetKey,
				"/participants/%s.%s/%.*s%s",
				EsifUp_GetName(upPtr), self->domainQualifier,
				(int) (ACPI_NAME_TARGET_SIZE - esif_ccb_min(esif_ccb_strlen(self->name, ACPI_NAME_TARGET_SIZE), ACPI_NAME_TARGET_SIZE)), "____",
				self->name);
			self->dataVaultKey = esif_ccb_strdup(targetKey);
		}
	}

exit:
	if (upPtr != NULL) {
		EsifUp_PutRef(upPtr);
	}
	return rc;
}

void TableObject_Destroy(TableObject *self)
{
	int i;
	for (i = 0; i < VARIANT_MAX_FIELDS; i++) {
		TableField_Destroy(&(self->fields[i]));
	}
	esif_ccb_free(self->name);
	esif_ccb_free(self->domainQualifier);
	esif_ccb_free(self->dataText);
	esif_ccb_free(self->binaryData);
	esif_ccb_free(self->dataXML);
	esif_ccb_free(self->dataVaultKey);
	esif_ccb_free(self->dataVaultNamespace);
	esif_ccb_free(self->dataVaultCategory);
	esif_ccb_memset(self, 0, sizeof(*self));
}
