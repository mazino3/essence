#define ES_INSTANCE_TYPE Instance
#include <essence.h>

// TODO Resizing the window after calling DirectoryEnumerateChildrenRecursively() is slow.
// TODO UTF-8 validation of outputted strings.
// TODO Check for heap allocation leaks.

struct Instance : EsInstance {
	EsThreadInformation scriptThread;
	char *inputText;
	size_t inputBytes;

	EsPanel *root;
	EsPanel *inputRow;
	EsPanel *outputPanel;
	EsSpacer *outputDecoration;
	EsTextbox *inputTextbox;

	char *outputLineBuffer;
	size_t outputLineBufferBytes;
	size_t outputLineBufferAllocated;
	bool anyOutput;
	bool gotREPLResult;
};

void AddPrompt(Instance *instance);
void AddOutput(Instance *instance, const char *text, size_t textBytes);

// --------------------------------- Script engine interface.

#define Assert EsAssert
#include <util/script.c>

void **fixedAllocationBlocks;
uint8_t *fixedAllocationCurrentBlock;
uintptr_t fixedAllocationCurrentPosition;
size_t fixedAllocationCurrentSize;

EsDirectoryChild *directoryIterationBuffer;
uintptr_t directoryIterationBufferPosition;
size_t directoryIterationBufferCount;

Instance *scriptInstance;

void *AllocateFixed(size_t bytes) {
	if (!bytes) {
		return nullptr;
	}

	bytes = (bytes + sizeof(uintptr_t) - 1) & ~(sizeof(uintptr_t) - 1);

	if (bytes >= fixedAllocationCurrentSize || fixedAllocationCurrentPosition >= fixedAllocationCurrentSize - bytes) {
		fixedAllocationCurrentSize = bytes > 1048576 ? bytes : 1048576;
		fixedAllocationCurrentPosition = 0;
		fixedAllocationCurrentBlock = (uint8_t *) EsCRTcalloc(1, fixedAllocationCurrentSize + sizeof(void *));

		if (!fixedAllocationCurrentBlock) {
			// TODO.
			EsAssert(false);
		}

		*(void **) fixedAllocationCurrentBlock = fixedAllocationBlocks;
		fixedAllocationBlocks = (void **) fixedAllocationCurrentBlock;
		fixedAllocationCurrentBlock += sizeof(void *);
	}

	void *p = fixedAllocationCurrentBlock + fixedAllocationCurrentPosition;
	fixedAllocationCurrentPosition += bytes;
	return p;
}

void *AllocateResize(void *old, size_t bytes) {
	return EsCRTrealloc(old, bytes);
}

int MemoryCompare(const void *a, const void *b, size_t bytes) {
	return EsCRTmemcmp(a, b, bytes);
}

void MemoryCopy(void *a, const void *b, size_t bytes) {
	EsMemoryCopy(a, b, bytes);
}

size_t PrintIntegerToBuffer(char *buffer, size_t bufferBytes, int64_t i) {
	EsCRTsnprintf(buffer, bufferBytes, "%ld", i);
	return EsCRTstrlen(buffer);
}

size_t PrintFloatToBuffer(char *buffer, size_t bufferBytes, double f) {
	EsCRTsnprintf(buffer, bufferBytes, "%f", f);
	return EsCRTstrlen(buffer);
}

void PrintDebug(const char *format, ...) {
	va_list arguments;
	va_start(arguments, format);
	char buffer[256];
	EsCRTvsnprintf(buffer, sizeof(buffer), format, arguments);
	AddOutput(scriptInstance, buffer, EsCStringLength(buffer)); 
	va_end(arguments);
}

void PrintError(Tokenizer *tokenizer, const char *format, ...) {
	PrintDebug("Error on line %d of '%s':\n", (int) tokenizer->line, tokenizer->module->path);
	va_list arguments;
	va_start(arguments, format);
	char buffer[256];
	EsCRTvsnprintf(buffer, sizeof(buffer), format, arguments);
	AddOutput(scriptInstance, buffer, EsCStringLength(buffer)); 
	va_end(arguments);
	PrintLine(tokenizer->module, tokenizer->line);
}

void PrintError2(Tokenizer *tokenizer, Node *node, const char *format, ...) {
	PrintDebug("Error on line %d of '%s':\n", (int) node->token.line, tokenizer->module->path);
	va_list arguments;
	va_start(arguments, format);
	char buffer[256];
	EsCRTvsnprintf(buffer, sizeof(buffer), format, arguments);
	AddOutput(scriptInstance, buffer, EsCStringLength(buffer)); 
	va_end(arguments);
	PrintLine(tokenizer->module, node->token.line);
}

void PrintError3(const char *format, ...) {
	PrintDebug("General error:\n");
	va_list arguments;
	va_start(arguments, format);
	char buffer[256];
	EsCRTvsnprintf(buffer, sizeof(buffer), format, arguments);
	AddOutput(scriptInstance, buffer, EsCStringLength(buffer)); 
	va_end(arguments);
}

void PrintError4(ExecutionContext *context, uint32_t instructionPointer, const char *format, ...) {
	LineNumber lineNumber = { 0 };
	LineNumberLookup(context, instructionPointer, &lineNumber);
	PrintDebug("Runtime error on line %d of '%s':\n", (int) lineNumber.lineNumber, 
			lineNumber.importData ? lineNumber.importData->path : "??");
	va_list arguments;
	va_start(arguments, format);
	char buffer[256];
	EsCRTvsnprintf(buffer, sizeof(buffer), format, arguments);
	AddOutput(scriptInstance, buffer, EsCStringLength(buffer)); 
	PrintLine(lineNumber.importData, lineNumber.lineNumber);
	PrintDebug("Back trace:\n");
	PrintBackTrace(context, instructionPointer, context->c, "");
}

void *FileLoad(const char *path, size_t *length) {
	return EsFileReadAll(path, -1, length);
}

CoroutineState *ExternalCoroutineWaitAny(ExecutionContext *context) {
	(void) context;
	EsAssert(false); // TODO.
	return nullptr;
}

int ExternalPrintStdErr(ExecutionContext *context, Value *returnValue) {
	(void) returnValue;
	STACK_POP_STRING(entryText, entryBytes);
	AddOutput(scriptInstance, entryText, entryBytes);
	return 1;
}

int ExternalPrintStdErrWarning(ExecutionContext *context, Value *returnValue) {
	return ExternalPrintStdErr(context, returnValue);
}

int ExternalPrintStdErrHighlight(ExecutionContext *context, Value *returnValue) {
	return ExternalPrintStdErr(context, returnValue);
}

int ExternalSystemSleepMs(ExecutionContext *context, Value *returnValue) {
	(void) returnValue;
	if (context->c->stackPointer < 1) return -1;
	int64_t ms = context->c->stack[--context->c->stackPointer].i;
	if (ms > 0) EsSleep(ms); 
	return 1;
}

int ExternalSystemGetHostName(ExecutionContext *context, Value *returnValue) {
	(void) context;
	RETURN_STRING_COPY("Essence", 7);
	return 3;
}

int ExternalSystemRunningAsAdministrator(ExecutionContext *context, Value *returnValue) {
	(void) context;
	returnValue->i = 0;
	return 2;
}

int ExternalSystemGetProcessorCount(ExecutionContext *context, Value *returnValue) {
	(void) context;
	returnValue->i = EsSystemGetOptimalWorkQueueThreadCount();
	return 2;
}

int ExternalRandomInt(ExecutionContext *context, Value *returnValue) {
	if (context->c->stackPointer < 2) return -1;
	int64_t min = context->c->stack[context->c->stackPointer - 1].i;
	int64_t max = context->c->stack[context->c->stackPointer - 2].i;
	if (max < min) { PrintError4(context, 0, "RandomInt() called with maximum limit (%ld) less than the minimum limit (%ld).\n", max, min); return 0; }
	returnValue->i = EsRandomU64() % (max - min + 1) + min;
	context->c->stackPointer -= 2;
	return 2;
}

int ExternalPathCreateDirectory(ExecutionContext *context, Value *returnValue) {
	(void) returnValue;
	STACK_POP_STRING(entryText, entryBytes);
	EsError error = EsPathCreate(entryText, entryBytes, ES_NODE_DIRECTORY, false);
	returnValue->i = error == ES_SUCCESS || error == ES_ERROR_FILE_ALREADY_EXISTS;
	return 2;
}

int ExternalPathExists(ExecutionContext *context, Value *returnValue) {
	(void) returnValue;
	STACK_POP_STRING(entryText, entryBytes);
	returnValue->i = EsPathExists(entryText, entryBytes) ? 1 : 0;
	return 2;
}

int ExternalPathIsFile(ExecutionContext *context, Value *returnValue) {
	(void) returnValue;
	STACK_POP_STRING(entryText, entryBytes);
	EsNodeType type;
	returnValue->i = EsPathExists(entryText, entryBytes, &type) ? 1 : 0;
	if (type != ES_NODE_FILE) returnValue->i = 0;
	return 2;
}

int ExternalPathIsDirectory(ExecutionContext *context, Value *returnValue) {
	(void) returnValue;
	STACK_POP_STRING(entryText, entryBytes);
	EsNodeType type;
	returnValue->i = EsPathExists(entryText, entryBytes, &type) ? 1 : 0;
	if (type != ES_NODE_DIRECTORY) returnValue->i = 0;
	return 2;
}

int ExternalPathIsLink(ExecutionContext *context, Value *returnValue) {
	(void) returnValue;
	STACK_POP_STRING(entryText, entryBytes);
	returnValue->i = 0;
	return 2;
}

int ExternalPathMove(ExecutionContext *context, Value *returnValue) {
	(void) returnValue;
	STACK_POP_STRING_2(entryText, entryBytes, entry2Text, entry2Bytes);
	returnValue->i = EsPathMove(entryText, entryBytes, entry2Text, entry2Bytes) == ES_SUCCESS;
	return 2;
}

int ExternalPathDelete(ExecutionContext *context, Value *returnValue) {
	(void) returnValue;
	STACK_POP_STRING(entryText, entryBytes);
	returnValue->i = EsPathDelete(entryText, entryBytes) == ES_SUCCESS;
	return 2;
}

int ExternalFileReadAll(ExecutionContext *context, Value *returnValue) {
	STACK_POP_STRING(entryText, entryBytes);
	returnValue->i = 0;
	size_t length = 0;
	void *data = EsFileReadAll(entryText, entryBytes, &length); // Free with EsHeapFree.
	if (!data) return 3;
	RETURN_STRING_NO_COPY((char *) data, length);
	return 3;
}

int ExternalFileWriteAll(ExecutionContext *context, Value *returnValue) {
	STACK_POP_STRING_2(entryText, entryBytes, entry2Text, entry2Bytes);
	returnValue->i = EsFileWriteAll(entryText, entryBytes, entry2Text, entry2Bytes) == ES_SUCCESS; 
	return 2;
}

int ExternalFileGetSize(ExecutionContext *context, Value *returnValue) {
	STACK_POP_STRING(entryText, entryBytes);
	EsDirectoryChild information;
	bool exists = EsPathQueryInformation(entryText, entryBytes, &information);
	returnValue->i = exists && information.type == ES_NODE_FILE ? information.fileSize : -1;
	return 2;
}

int ExternalFileCopy(ExecutionContext *context, Value *returnValue) {
	STACK_POP_STRING_2(entryText, entryBytes, entry2Text, entry2Bytes);
	returnValue->i = EsFileCopy(entryText, entryBytes, entry2Text, entry2Bytes) == ES_SUCCESS;
	return 2;
}

int External_DirectoryInternalStartIteration(ExecutionContext *context, Value *returnValue) {
	STACK_POP_STRING(entryText, entryBytes);
	EsHeapFree(directoryIterationBuffer);
	directoryIterationBuffer = nullptr;
	ptrdiff_t count = EsDirectoryEnumerateChildren(entryText, entryBytes, &directoryIterationBuffer);
	returnValue->i = count >= 0 ? 1 : 0;
	directoryIterationBufferPosition = 0;
	directoryIterationBufferCount = count >= 0 ? count : 0;
	return 2;
}

int External_DirectoryInternalEndIteration(ExecutionContext *context, Value *returnValue) {
	(void) context;
	(void) returnValue;
	EsHeapFree(directoryIterationBuffer);
	directoryIterationBuffer = nullptr;
	directoryIterationBufferPosition = 0;
	directoryIterationBufferCount = 0;
	return 1;
}

int External_DirectoryInternalNextIteration(ExecutionContext *context, Value *returnValue) {
	(void) context;

	if (directoryIterationBufferPosition == directoryIterationBufferCount) {
		returnValue->i = 0;
	} else { 
		RETURN_STRING_COPY(directoryIterationBuffer[directoryIterationBufferPosition].name, 
				directoryIterationBuffer[directoryIterationBufferPosition].nameBytes); 
		directoryIterationBufferPosition++;
	}

	return 3;
}

#define EXTERNAL_STUB(name) int name(ExecutionContext *, Value *) { EsPrint("Unimplemented " #name "\n"); EsAssert(false); return -1; }

// TODO What to do with these?
EXTERNAL_STUB(ExternalPathGetDefaultPrefix);
EXTERNAL_STUB(ExternalPathSetDefaultPrefixToScriptSourceDirectory);

// TODO These functions should be moved to Helpers.
EXTERNAL_STUB(ExternalPersistRead);
EXTERNAL_STUB(ExternalPersistWrite);

// TODO Functions only available in the POSIX subsystem:
EXTERNAL_STUB(ExternalConsoleGetLine);
EXTERNAL_STUB(ExternalSystemShellExecute);
EXTERNAL_STUB(ExternalSystemShellExecuteWithWorkingDirectory);
EXTERNAL_STUB(ExternalSystemShellEvaluate);
EXTERNAL_STUB(ExternalSystemShellEnableLogging);
EXTERNAL_STUB(ExternalSystemGetEnvironmentVariable);
EXTERNAL_STUB(ExternalSystemSetEnvironmentVariable);

// --------------------------------- User interface.

#define COLOR_BACKGROUND (0xFFFDFDFD)
#define COLOR_ROW_ODD (0xFFF4F4F4)
#define COLOR_ROW_HEADER_LINE (0xFFCCCCCC)
#define COLOR_OUTPUT_DECORATION_IN_PROGRESS (0xFFFF7F00)
#define COLOR_OUTPUT_DECORATION_SUCCESS (0xFF3070FF)
#define COLOR_OUTPUT_DECORATION_FAILURE (0xFFFF3040)
#define COLOR_TEXT_MAIN (0xFF010102)
#define COLOR_TEXT_LIGHT (0xFF606062)
#define TEXT_SIZE_DEFAULT (14)
#define TEXT_SIZE_OUTPUT (12)

const EsStyle styleBackground = {
	.appearance = {
		.enabled = true,
		.backgroundColor = COLOR_BACKGROUND,
	},
};

const EsStyle styleRoot = {
	.metrics = {
		.mask = ES_THEME_METRICS_INSETS | ES_THEME_METRICS_GAP_ALL,
		.insets = ES_RECT_4(32, 32, 32, 32),
		.gapMajor = 4,
	},
};

const EsStyle styleInputRow = {
	.metrics = {
		.mask = ES_THEME_METRICS_GAP_ALL,
		.gapMajor = 8,
	},
};

const EsStyle stylePathDefaultPrefixDisplay = {
	.metrics = {
		.mask = ES_THEME_METRICS_FONT_FAMILY | ES_THEME_METRICS_FONT_WEIGHT
			| ES_THEME_METRICS_TEXT_SIZE | ES_THEME_METRICS_TEXT_ALIGN | ES_THEME_METRICS_TEXT_COLOR,
		.textColor = COLOR_TEXT_MAIN,
		.textAlign = ES_TEXT_H_LEFT | ES_TEXT_WRAP | ES_TEXT_V_TOP,
		.textSize = TEXT_SIZE_DEFAULT,
		.fontFamily = ES_FONT_SANS,
		.fontWeight = 6,
	},
};

const EsStyle styleOutputParagraph = {
	.metrics = {
		.mask = ES_THEME_METRICS_FONT_FAMILY | ES_THEME_METRICS_FONT_WEIGHT
			| ES_THEME_METRICS_TEXT_SIZE | ES_THEME_METRICS_TEXT_ALIGN | ES_THEME_METRICS_TEXT_COLOR,
		.textColor = COLOR_TEXT_MAIN,
		.textAlign = ES_TEXT_H_LEFT | ES_TEXT_WRAP | ES_TEXT_V_TOP,
		.textSize = TEXT_SIZE_OUTPUT,
		.fontFamily = ES_FONT_SANS,
		.fontWeight = 4,
	},
};

const EsStyle styleOutputParagraphStrong = {
	.metrics = {
		.mask = ES_THEME_METRICS_FONT_FAMILY | ES_THEME_METRICS_FONT_WEIGHT
			| ES_THEME_METRICS_TEXT_SIZE | ES_THEME_METRICS_TEXT_ALIGN | ES_THEME_METRICS_TEXT_COLOR,
		.textColor = COLOR_TEXT_MAIN,
		.textAlign = ES_TEXT_H_LEFT | ES_TEXT_WRAP | ES_TEXT_V_TOP,
		.textSize = TEXT_SIZE_OUTPUT,
		.fontFamily = ES_FONT_SANS,
		.fontWeight = 6,
	},
};

const EsStyle styleOutputParagraphItalic = {
	.metrics = {
		.mask = ES_THEME_METRICS_FONT_FAMILY | ES_THEME_METRICS_FONT_WEIGHT | ES_THEME_METRICS_IS_ITALIC
			| ES_THEME_METRICS_TEXT_SIZE | ES_THEME_METRICS_TEXT_ALIGN | ES_THEME_METRICS_TEXT_COLOR,
		.textColor = COLOR_TEXT_MAIN,
		.textAlign = ES_TEXT_H_LEFT | ES_TEXT_WRAP | ES_TEXT_V_TOP,
		.textSize = TEXT_SIZE_OUTPUT,
		.fontFamily = ES_FONT_SANS,
		.fontWeight = 4,
		.isItalic = true,
	},
};

const EsStyle stylePromptText = {
	.metrics = {
		.mask = ES_THEME_METRICS_FONT_FAMILY | ES_THEME_METRICS_FONT_WEIGHT
			| ES_THEME_METRICS_TEXT_SIZE | ES_THEME_METRICS_TEXT_ALIGN | ES_THEME_METRICS_TEXT_COLOR,
		.textColor = COLOR_TEXT_LIGHT,
		.textAlign = ES_TEXT_H_LEFT | ES_TEXT_V_CENTER,
		.textSize = TEXT_SIZE_DEFAULT,
		.fontFamily = ES_FONT_SANS,
		.fontWeight = 5,
	},
};

const EsStyle styleInputTextbox = {
	.inherit = ES_STYLE_TEXTBOX_TRANSPARENT,

	.metrics = {
		.mask = ES_THEME_METRICS_FONT_FAMILY | ES_THEME_METRICS_FONT_WEIGHT
			| ES_THEME_METRICS_TEXT_SIZE | ES_THEME_METRICS_TEXT_COLOR,
		.textColor = COLOR_TEXT_MAIN,
		.textSize = TEXT_SIZE_DEFAULT,
		.fontFamily = ES_FONT_SANS,
		.fontWeight = 4,
	},
};

const EsStyle styleCommandLogText = {
	.metrics = {
		.mask = ES_THEME_METRICS_FONT_FAMILY | ES_THEME_METRICS_FONT_WEIGHT
			| ES_THEME_METRICS_TEXT_SIZE | ES_THEME_METRICS_TEXT_ALIGN | ES_THEME_METRICS_TEXT_COLOR,
		.textColor = COLOR_TEXT_MAIN,
		.textAlign = ES_TEXT_H_LEFT | ES_TEXT_ELLIPSIS | ES_TEXT_V_TOP,
		.textSize = TEXT_SIZE_DEFAULT,
		.fontFamily = ES_FONT_SANS,
		.fontWeight = 4,
	},
};

const EsStyle styleInterCommandSpacer = {
	.metrics = {
		.mask = ES_THEME_METRICS_PREFERRED_HEIGHT,
		.preferredHeight = 14,
	},
};

const EsStyle styleOutputPanelWrapper = {
	.metrics = {
		.mask = ES_THEME_METRICS_INSETS | ES_THEME_METRICS_GAP_MAJOR,
		.insets = ES_RECT_1(10),
		.gapMajor = 12,
	},
};

const EsStyle styleOutputPanel = {
	.metrics = {
		.mask = ES_THEME_METRICS_INSETS | ES_THEME_METRICS_GAP_MAJOR,
		.insets = ES_RECT_2(0, 4),
		.gapMajor = 0,
	},
};

const EsStyle styleOutputDecorationInProgress = {
	.metrics = {
		.mask = ES_THEME_METRICS_PREFERRED_WIDTH,
		.preferredWidth = 6,
	},

	.appearance = {
		.enabled = true,
		.backgroundColor = COLOR_OUTPUT_DECORATION_IN_PROGRESS,
	},
};

const EsStyle styleOutputDecorationSuccess = {
	.metrics = {
		.mask = ES_THEME_METRICS_PREFERRED_WIDTH,
		.preferredWidth = 6,
	},

	.appearance = {
		.enabled = true,
		.backgroundColor = COLOR_OUTPUT_DECORATION_SUCCESS,
	},
};

const EsStyle styleOutputDecorationFailure = {
	.metrics = {
		.mask = ES_THEME_METRICS_PREFERRED_WIDTH,
		.preferredWidth = 6,
	},

	.appearance = {
		.enabled = true,
		.backgroundColor = COLOR_OUTPUT_DECORATION_FAILURE,
	},
};

const EsThemeAppearance styleAppearanceRowHeader = {
	.enabled = true,
	.backgroundColor = COLOR_BACKGROUND,
	.borderColor = COLOR_ROW_HEADER_LINE,
	.borderSize = ES_RECT_4(0, 0, 0, 1),
};

const EsThemeAppearance styleAppearanceRowEven = {
	.enabled = true,
	.backgroundColor = COLOR_BACKGROUND,
};

const EsThemeAppearance styleAppearanceRowOdd = {
	.enabled = true,
	.backgroundColor = COLOR_ROW_ODD,
};

const EsStyle styleListRowEven = {
	.metrics = {
		.mask = ES_THEME_METRICS_INSETS,
		.insets = ES_RECT_1(3),
	},

	.appearance = styleAppearanceRowEven,
};

const EsStyle styleListRowOdd = {
	.metrics = {
		.mask = ES_THEME_METRICS_INSETS,
		.insets = ES_RECT_1(3),
	},

	.appearance = styleAppearanceRowOdd,
};

void AddREPLResult(ExecutionContext *context, EsElement *parent, Node *type, Value value) {
	// TODO Truncating/scrolling/collapsing/saving/copying output.
	// TODO Letting scripts register custom views for structs.

	size_t bytes;

	if (type->type == T_INT) {
		char *buffer = EsStringAllocateAndFormat(&bytes, "%d", value.i);
		EsTextDisplayCreate(parent, ES_CELL_H_FILL, &styleOutputParagraphStrong, buffer, bytes);
	} else if (type->type == T_BOOL) {
		char *buffer = EsStringAllocateAndFormat(&bytes, "%z", value.i ? "true" : "false");
		EsTextDisplayCreate(parent, ES_CELL_H_FILL, &styleOutputParagraphStrong, buffer, bytes);
	} else if (type->type == T_NULL) {
		char *buffer = EsStringAllocateAndFormat(&bytes, "%z", "null");
		EsTextDisplayCreate(parent, ES_CELL_H_FILL, &styleOutputParagraphStrong, buffer, bytes);
	} else if (type->type == T_FLOAT) {
		char *buffer = EsStringAllocateAndFormat(&bytes, "%F", value.f);
		EsTextDisplayCreate(parent, ES_CELL_H_FILL, &styleOutputParagraphStrong, buffer, bytes);
	} else if (type->type == T_STR) {
		EsAssert(context->heapEntriesAllocated > (uint64_t) value.i);
		HeapEntry *entry = &context->heap[value.i];
		const char *valueText;
		size_t valueBytes;
		ScriptHeapEntryToString(context, entry, &valueText, &valueBytes);
		char *buffer = EsStringAllocateAndFormat(&bytes, "\"%s\"", valueBytes, valueText);
		EsTextDisplayCreate(parent, ES_CELL_H_FILL, &styleOutputParagraphStrong, buffer, bytes);
	} else if (type->type == T_LIST && type->firstChild->type == T_STRUCT) {
		// TODO Row styling.

		EsAssert(context->heapEntriesAllocated > (uint64_t) value.i);
		HeapEntry *listEntry = &context->heap[value.i];
		EsAssert(listEntry->type == T_LIST);
		if (!listEntry->length) goto normalList;
		EsAssert(context->heapEntriesAllocated > (uint64_t) listEntry->list[0].i);
		EsAssert(context->heap[listEntry->list[0].i].type == T_STRUCT);

		EsPanel *table = EsPanelCreate(parent, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL | ES_PANEL_TABLE);
		EsPanelSetBands(table, context->heap[listEntry->list[0].i].fieldCount);
		EsPanelTableAddBandDecorator(table, { .index = 0, .repeatEvery = 0, .axis = 1, .appearance = &styleAppearanceRowHeader });
		EsPanelTableAddBandDecorator(table, { .index = 1, .repeatEvery = 2, .axis = 1, .appearance = &styleAppearanceRowEven });
		EsPanelTableAddBandDecorator(table, { .index = 2, .repeatEvery = 2, .axis = 1, .appearance = &styleAppearanceRowOdd });

		{
			Node *field = type->firstChild->firstChild;

			while (field) {
				EsTextDisplayCreate(table, ES_CELL_H_FILL, &styleOutputParagraph, field->token.text, field->token.textBytes);
				field = field->sibling;
			}
		}

		for (uintptr_t i = 0; i < listEntry->length; i++) {
			EsAssert(context->heapEntriesAllocated > (uint64_t) value.i);
			HeapEntry *itemEntry = &context->heap[listEntry->list[i].i];
			EsAssert(itemEntry->type == T_STRUCT);
			EsAssert(itemEntry->fieldCount == context->heap[listEntry->list[0].i].fieldCount);
			Node *field = type->firstChild->firstChild;
			uintptr_t j = 0;

			while (field) {
				EsAssert(j != itemEntry->fieldCount);
				AddREPLResult(context, table, field->firstChild, itemEntry->fields[j]);
				field = field->sibling;
				j++;
			}
		}
	} else if (type->type == T_LIST) {
		normalList:;
		EsPanel *panel = EsPanelCreate(parent, ES_CELL_H_FILL | ES_PANEL_VERTICAL | ES_PANEL_STACK);
		EsAssert(context->heapEntriesAllocated > (uint64_t) value.i);
		HeapEntry *entry = &context->heap[value.i];
		EsAssert(entry->type == T_LIST);

		if (!entry->length) {
			EsTextDisplayCreate(parent, ES_CELL_H_FILL, &styleOutputParagraphItalic, 
					EsLiteral("Empty list.\n"));
		}

		for (uintptr_t i = 0; i < entry->length; i++) {
			EsPanel *item = EsPanelCreate(panel, ES_CELL_H_FILL, (i % 2) ? &styleListRowOdd : &styleListRowEven);
			AddREPLResult(context, item, type->firstChild, entry->list[i]);
		}
	} else if (type->type == T_STRUCT) {
		EsPanel *panel = EsPanelCreate(parent, ES_CELL_H_FILL | ES_PANEL_VERTICAL | ES_PANEL_STACK);
		EsAssert(context->heapEntriesAllocated > (uint64_t) value.i);
		HeapEntry *entry = &context->heap[value.i];
		EsAssert(entry->type == T_STRUCT);
		uintptr_t i = 0;

		Node *field = type->firstChild;

		while (field) {
			EsAssert(i != entry->fieldCount);
			EsPanel *item = EsPanelCreate(panel, ES_CELL_H_FILL, (i % 2) ? &styleListRowOdd : &styleListRowEven);
			AddREPLResult(context, item, field->firstChild, entry->fields[i]);
			field = field->sibling;
			i++;
		}
	} else if (type->type == T_FUNCPTR) {
		EsTextDisplayCreate(parent, ES_CELL_H_FILL, &styleOutputParagraphItalic, 
				EsLiteral("Function pointer.\n"));
	} else {
		EsTextDisplayCreate(parent, ES_CELL_H_FILL, &styleOutputParagraphItalic, 
				EsLiteral("The type of the result was not recognized.\n"));
	}
}

void ExternalPassREPLResult(ExecutionContext *context, Value value) {
	Instance *instance = scriptInstance;

	if (instance->gotREPLResult) return;
	if (instance->outputLineBufferBytes) AddOutput(instance, EsLiteral("\n"));

	instance->anyOutput = true;
	instance->gotREPLResult = true;

	EsMessageMutexAcquire();
	AddREPLResult(context, instance->outputPanel, context->functionData->replResultType, value);
	EsMessageMutexRelease();
}

void ScriptThread(EsGeneric _instance) {
	Instance *instance = (Instance *) _instance.p;
	scriptInstance = instance;
	int result = ScriptExecuteFromFile(EsLiteral("in"), instance->inputText, instance->inputBytes, true);
	instance->inputText = nullptr;
	
	if (instance->outputLineBufferBytes) {
		AddOutput(instance, EsLiteral("\n"));
	}

	EsMessageMutexAcquire();

	if (!instance->anyOutput) {
		EsElementDestroy(EsElementGetLayoutParent(instance->outputPanel));
	} else {
		instance->anyOutput = false;
	}

	instance->gotREPLResult = false;

	if (result == 0) {
		EsSpacerChangeStyle(instance->outputDecoration, &styleOutputDecorationSuccess);
	} else {
		EsSpacerChangeStyle(instance->outputDecoration, &styleOutputDecorationFailure);
	}

	EsSpacerCreate(instance->root, ES_CELL_H_FILL, &styleInterCommandSpacer);
	AddPrompt(instance);

	EsMessageMutexRelease();
}

void EnterCommand(Instance *instance) {
	EsAssert(instance->inputTextbox);
	EsAssert(instance->inputRow);
	size_t dataBytes;
	char *data = EsTextboxGetContents(instance->inputTextbox, &dataBytes, ES_FLAGS_DEFAULT);
	EsElementDestroy(instance->inputRow);
	instance->inputTextbox = nullptr;
	instance->inputRow = nullptr;

	EsPanel *commandLogRow = EsPanelCreate(instance->root, ES_CELL_H_FILL | ES_PANEL_STACK | ES_PANEL_HORIZONTAL, &styleInputRow);
	EsTextDisplayCreate(commandLogRow, ES_FLAGS_DEFAULT, &stylePromptText, "\u2661");
	EsTextDisplayCreate(commandLogRow, ES_CELL_H_FILL, &styleCommandLogText, data, dataBytes);

	EsAssert(!instance->outputPanel);
	EsAssert(!instance->outputDecoration);
	EsPanel *outputPanelWrapper = EsPanelCreate(instance->root, ES_CELL_H_FILL | ES_PANEL_STACK | ES_PANEL_HORIZONTAL, &styleOutputPanelWrapper);
	instance->outputDecoration = EsSpacerCreate(outputPanelWrapper, ES_CELL_V_FILL, &styleOutputDecorationInProgress);
	instance->outputPanel = EsPanelCreate(outputPanelWrapper, ES_CELL_H_FILL | ES_PANEL_STACK | ES_PANEL_VERTICAL, &styleOutputPanel);

	instance->inputText = data;
	instance->inputBytes = dataBytes;
	EsAssert(ES_SUCCESS == EsThreadCreate(ScriptThread, &instance->scriptThread, instance)); 
	EsHandleClose(instance->scriptThread.handle);
}

int InputTextboxMessage(EsElement *element, EsMessage *message) {
	if (message->type == ES_MSG_KEY_TYPED) {
		if (message->keyboard.scancode == ES_SCANCODE_ENTER) {
			EnterCommand(element->instance);
			return ES_HANDLED;
		}
	}

	return 0;
}

void AddOutput(Instance *instance, const char *text, size_t textBytes) {
	instance->anyOutput = true;

	for (uintptr_t i = 0; i < textBytes; i++) {
		if (text[i] == '\n') {
			EsMessageMutexAcquire();
			EsTextDisplayCreate(instance->outputPanel, ES_CELL_H_FILL, &styleOutputParagraph, 
					instance->outputLineBuffer, instance->outputLineBufferBytes);
			EsMessageMutexRelease();
			instance->outputLineBufferBytes = 0;
		} else {
			if (instance->outputLineBufferBytes == instance->outputLineBufferAllocated) {
				instance->outputLineBufferAllocated = (instance->outputLineBufferAllocated + 4) * 2;
				instance->outputLineBuffer = (char *) EsHeapReallocate(instance->outputLineBuffer, 
						instance->outputLineBufferAllocated, false);
			}

			instance->outputLineBuffer[instance->outputLineBufferBytes++] = text[i];
		}
	}
}

void AddPrompt(Instance *instance) {
	EsAssert(!instance->inputTextbox);
	EsAssert(!instance->inputRow);
	instance->outputPanel = nullptr;
	instance->outputDecoration = nullptr;

	EsTextDisplayCreate(instance->root, ES_CELL_H_FILL, &stylePathDefaultPrefixDisplay, "Essence HD (0:)");
	instance->inputRow = EsPanelCreate(instance->root, ES_CELL_H_FILL | ES_PANEL_STACK | ES_PANEL_HORIZONTAL, &styleInputRow);
	EsTextDisplayCreate(instance->inputRow, ES_FLAGS_DEFAULT, &stylePromptText, "\u2665");
	instance->inputTextbox = EsTextboxCreate(instance->inputRow, ES_CELL_H_FILL, &styleInputTextbox);
	EsTextboxEnableSmartReplacement(instance->inputTextbox, false);
	instance->inputTextbox->messageUser = InputTextboxMessage;
	EsElementFocus(instance->inputTextbox, ES_ELEMENT_FOCUS_ENSURE_VISIBLE);
}

int InstanceCallback(Instance *instance, EsMessage *message) {
	if (message->type == ES_MSG_INSTANCE_DESTROY) {
		// TODO Stopping the script thread.
		EsHeapFree(instance->outputLineBuffer);
	}

	return 0;
}

void _start() {
	_init();

	// TODO Proper drive mounting.

	EsDeviceEnumerate([] (EsMessageDevice device, EsGeneric) {
		if (device.type == ES_DEVICE_FILE_SYSTEM && EsDeviceControl(device.handle, ES_DEVICE_CONTROL_FS_IS_BOOT, 0, nullptr)) {
			EsMountPointAdd(EsLiteral("0:"), device.handle);
		}
	}, 0);

	while (true) {
		EsMessage *message = EsMessageReceive();

		if (message->type == ES_MSG_INSTANCE_CREATE) {
			Instance *instance = EsInstanceCreate(message, "Script Console", -1);
			instance->callback = InstanceCallback;
			EsWindowSetIcon(instance->window, ES_ICON_UTILITIES_TERMINAL);
			EsPanel *wrapper = EsPanelCreate(instance->window, ES_CELL_FILL, ES_STYLE_PANEL_WINDOW_DIVIDER);
			EsPanel *background = EsPanelCreate(wrapper, ES_CELL_FILL | ES_PANEL_V_SCROLL_AUTO, &styleBackground);
			instance->root = EsPanelCreate(background, ES_CELL_H_FILL | ES_PANEL_STACK | ES_PANEL_VERTICAL, &styleRoot);
			AddPrompt(instance);
		}
	}
}
