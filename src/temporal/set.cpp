#include "temporal/set.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include "duckdb/function/function_set.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include "time_util.hpp"
#include "mobilityduck/meos_exec_serial.hpp"

extern "C" {
    #include "meos.h"    
    #include "meos_internal.h"   
    #include "meos_geo.h"
}

#ifndef WKB_EXTENDED
#define WKB_EXTENDED ((uint8_t)0x04)
#endif

namespace duckdb {

// Collection type registration (accessors, RegisterTypes, AllTypes, alias->MeosType,
// GetChildType) is generated from the catalog MeosType enum into
// src/generated/generated_type_registration.cpp.

// Register all cast functions
void SetTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    for (const auto &set_type : SetTypes::AllTypes()) {
        RegisterMeosCastFunction(loader, 
            set_type,                      
            LogicalType::VARCHAR,   
            SetFunctions::Set_to_text   
        ); // Blob to text
        RegisterMeosCastFunction(loader, 
            LogicalType::VARCHAR, 
            set_type,                                    
            SetFunctions::Text_to_set   
        ); // text to blob
        
        auto base_type = SetTypeMapping::GetChildType(set_type);
        RegisterMeosCastFunction(loader, 
            base_type,
            set_type,
            SetFunctions::Value_to_set_cast // set from base type
        );

        RegisterMeosCastFunction(loader, 
            SetTypes::intset(),
            SetTypes::floatset(),
            SetFunctions::Intset_to_floatset_cast // intset -> floatset 
        );

        RegisterMeosCastFunction(loader, 
            SetTypes::floatset(),
            SetTypes::intset(),
            SetFunctions::Floatset_to_intset_cast // floatset --> intset
        );
        
        RegisterMeosCastFunction(loader, 
            SetTypes::dateset(),
            SetTypes::tstzset(),
            SetFunctions::Dateset_to_tstzset_cast // dateset -> tstzset
        );
        
        RegisterMeosCastFunction(loader, 
            SetTypes::tstzset(),
            SetTypes::dateset(),
            SetFunctions::Tstzset_to_dateset_cast // tstz -> dateset 
        );

    }
}

// --- Unnest ---
struct SetUnnestBindData : public TableFunctionData {
    string_t blob;
    MeosType set_type;
    LogicalType return_type;

    SetUnnestBindData(string_t blob, MeosType set_type, LogicalType return_type)
        : blob(std::move(blob)), set_type(set_type), return_type(std::move(return_type)) {}
};

struct SetUnnestGlobalState : public GlobalTableFunctionState {
    idx_t idx = 0;
    std::vector<Value> values;    
};

static unique_ptr<FunctionData> SetUnnestBind(ClientContext &context,
                                              TableFunctionBindInput &input,
                                              vector<LogicalType> &return_types,
                                              vector<string> &names) {
    if (input.inputs.size() != 1 || input.inputs[0].IsNull()) {
        throw BinderException("SetUnnest: expects a non-null blob input");
    }

    auto in_val = input.inputs[0];
    if (in_val.type().id() != LogicalTypeId::BLOB) {
        throw BinderException("SetUnnest: expected BLOB as input");
    }

    string_t blob = StringValue::Get(in_val);

    auto duck_type = SetTypeMapping::GetChildType(in_val.type());
    auto set_type = SetTypeMapping::GetMeosTypeFromAlias(in_val.type().ToString());

    return_types.emplace_back(duck_type);
    names.emplace_back("unnest");

    return make_uniq<SetUnnestBindData>(blob, set_type, duck_type);
}

static unique_ptr<GlobalTableFunctionState> SetUnnestInit(ClientContext &context,
                                                          TableFunctionInitInput &input) {
    auto &bind = input.bind_data->Cast<SetUnnestBindData>();
    auto &blob = bind.blob;

    const uint8_t *data = (const uint8_t *)blob.GetData();
    size_t size = blob.GetSize();

    Set *s = (Set *)malloc(size);
    memcpy(s, data, size);

    auto state = make_uniq<SetUnnestGlobalState>();
    int count = s->count;

    for (int i = 1; i <= count; ++i) {
        Datum d;
        bool found = set_value_n(s, i, &d);
        if (!found) continue;

        switch (settype_basetype(bind.set_type)) {
            case T_INT4:
                state->values.emplace_back(Value::INTEGER((int32_t)d));
                break;
            case T_INT8:
                state->values.emplace_back(Value::BIGINT((int64_t)d));
                break;
            case T_FLOAT8:
                state->values.emplace_back(Value::DOUBLE(DatumGetFloat8(d)));
                break;
            case T_TEXT: {     
                text *txt = (text *)DatumGetPointer(d);
                int len = VARSIZE(txt) - VARHDRSZ;
                std::string str(VARDATA(txt), len);
                state->values.emplace_back(Value(str));
                break;
            }
            case T_DATE:
                state->values.emplace_back(Value::DATE(date_t(FromMeosDate((int32_t)d))));
                break;
            case T_TIMESTAMPTZ:
                state->values.emplace_back(Value::TIMESTAMPTZ(timestamp_tz_t(FromMeosTimestamp((int64_t)d))));
                break;
            default: {
                // A BLOB-backed base value: hand back the varlena itself, typed as the
                // element type the bind resolved, so unnest answers the registered type
                // rather than a bare BLOB.
                MeosType elem_type = settype_basetype(bind.set_type);
                Value v = basetype_byvalue(elem_type)
                    ? Value::BIGINT((int64_t) d)
                    : Value::BLOB((const_data_ptr_t) DatumGetPointer(d),
                                  meostype_length(elem_type) == -1
                                      ? VARSIZE(DatumGetPointer(d))
                                      : (idx_t) meostype_length(elem_type));
                v.GetTypeMutable() = bind.return_type;
                state->values.emplace_back(std::move(v));
                break;
            }
        }
    }

    free(s);
    return std::move(state);
}

static void SetUnnestExec(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
    auto &state = input.global_state->Cast<SetUnnestGlobalState>();
    auto count = MinValue<idx_t>(STANDARD_VECTOR_SIZE, state.values.size() - state.idx);

    for (idx_t i = 0; i < count; i++) {
        output.SetValue(0, i, state.values[state.idx++]);
    }

    output.SetCardinality(count);
}

void SetTypes::RegisterSetUnnest(ExtensionLoader &loader) {
    for (auto &set_type : SetTypes::AllTypes()) {
        TableFunction fn("setUnnest",
                         {set_type},
                         SetUnnestExec,
                         SetUnnestBind,
                         SetUnnestInit);
        RegisterMeosFunction(loader, fn);
    }
}

} // namespace duckdb
