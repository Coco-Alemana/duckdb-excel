#include "xlsx/read_xlsx.hpp"

#include "duckdb.hpp"
#include "duckdb/common/helper.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/types.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/extension_util.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/tableref/table_function_ref.hpp"
#include "duckdb/function/replacement_scan.hpp"

#include "xlsx/zip_file.hpp"
#include "xlsx/parsers/workbook_parser.hpp"
#include "xlsx/parsers/relationship_parser.hpp"

namespace duckdb {

struct SheetListData : public FunctionData {
	string file_path;

	unique_ptr<FunctionData> Copy() const override {
		return make_uniq<SheetListData>(*this);
	}

	bool Equals(const FunctionData &other_p) const override {
		const auto &other = other_p.Cast<SheetListData>();
		return file_path == other.file_path;
	}
};

static unique_ptr<FunctionData> SheetListBind(ClientContext &context, TableFunctionBindInput &input,
                                              vector<LogicalType> &return_types, vector<string> &names) {
	auto result = make_uniq<SheetListData>();
	result->file_path = StringValue::Get(input.inputs[0]);

	return_types.emplace_back(LogicalType::VARCHAR);
	names.emplace_back("sheet_name");

	return std::move(result);
}

class SheetListState : public GlobalTableFunctionState {
public:
	vector<string> sheets;
	size_t current_idx = 0;
};

static unique_ptr<GlobalTableFunctionState> SheetListInit(ClientContext &context, TableFunctionInitInput &input) {
	auto &data = input.bind_data->Cast<SheetListData>();
	auto state = make_uniq<SheetListState>();

	ZipFileReader reader(context, data.file_path);

	if (!reader.TryOpenEntry("xl/workbook.xml")) {
		throw IOException("Could not find xl/workbook.xml in xlsx file");
	}
	auto sheets = WorkBookParser::GetSheets(reader);
	reader.CloseEntry();

	for (auto &sheet : sheets) {
		state->sheets.push_back(sheet.first);
	}

	return std::move(state);
}

static void SheetListFunction(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
	auto &state = input.global_state->Cast<SheetListState>();
	auto &sheets = state.sheets;
	auto &idx = state.current_idx;

	idx_t count = 0;
	while (idx < sheets.size() && count < STANDARD_VECTOR_SIZE) {
		output.SetValue(0, count, Value(sheets[idx]));
		idx++;
		count++;
	}

	output.SetCardinality(count);
}

TableFunction XLSX_Sheets::GetFunction() {
	TableFunction func("xlsx_sheets", {LogicalType::VARCHAR}, SheetListFunction, SheetListBind, SheetListInit);
	return func;
}

void XLSX_Sheets::Register(DatabaseInstance &db) {
	ExtensionUtil::RegisterFunction(db, GetFunction());
}

} // namespace duckdb
