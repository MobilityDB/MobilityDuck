# DuckDB Custom Types Implementation Guide

This guide demonstrates how to create custom types in DuckDB.

## I. Type Definition and Registration

### 1. Define the Logical Type

Create a function that returns your custom logical type:

```cpp
LogicalType TGeometryTypes::TGEOMETRY() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("TGEOMETRY");
    return type;
}
```

**Key Points:**
- Custom types are typically stored as `BLOB` internally for complex data structures
- Use `SetAlias()` to give your type a meaningful name that appears in SQL queries
- The alias name should follow SQL naming conventions (uppercase recommended)

### 2. Register the Type

Register your custom type with the database instance:

```cpp
void TGeometryTypes::RegisterTypes(DatabaseInstance &instance) {
    ExtensionUtil::RegisterType(instance, "TGEOMETRY", TGeometryTypes::TGEOMETRY());
}
```

## II. Scalar Functions Implementation
Scalar functions are the bridge between SQL and your C++ code — they convert between DuckDB types and your library’s types (e.g., MEOS).

### 1. Constructor Functions

Constructor functions create instances of your custom type from input parameters:

```cpp
inline void Tgeoinst_constructor(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &value_vec = args.data[0];    // Geometry string input
    auto &t_vec = args.data[1];        // Timestamp input

    BinaryExecutor::Execute<string_t, timestamp_tz_t, string_t>(
        value_vec, t_vec, result, count,
        [&](string_t value_str, timestamp_tz_t t) -> string_t {
            std::string value = value_str.GetString();
            
            // Parse geometry from input string
            GSERIALIZED *gs = geom_in(value.c_str(), -1); // -1 = no typmod constraint
            if (gs == NULL) {
                throw InvalidInputException("Invalid geometry format: " + value);
            }

            // Convert DuckDB timestamp to MEOS timestamp
            timestamp_tz_t meos_timestamp = DuckDBToMeosTimestamp(t);
            
            // Create temporal geometry instant
            TInstant *inst = tgeoinst_make(gs, static_cast<TimestampTz>(meos_timestamp.value));
            if (inst == NULL) {
                free(gs);
                throw InvalidInputException("Failed to create TInstant");
            }

            // Serialize the temporal object to binary data
            size_t data_size = temporal_mem_size((Temporal*)inst);
            uint8_t *data_buffer = (uint8_t *)malloc(data_size);
            
            if (!data_buffer) {
                free(inst);
                throw InvalidInputException("Failed to allocate memory for geometry data");
            }
            
            memcpy(data_buffer, inst, data_size);
            
            // Store as binary string in DuckDB
            string_t data_string_t(reinterpret_cast<const char*>(data_buffer), data_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, data_string_t);
            
            // Clean up allocated memory
            free(data_buffer);
            free(inst);  
            
            return stored_data;
        });

    // Optimize for single-value results
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}
```

**Best Practices:**
- Always validate input parameters and throw meaningful exceptions
- Use appropriate memory management (allocate/free pairs)
- Use `StringVector::AddStringOrBlob()` for binary data storage
- Set vector type to `CONSTANT_VECTOR` for single-value operations

### 2. Cast Functions

Cast functions enable conversion between your custom type and standard SQL types:

#### String to Custom Type
```cpp
bool TgeometryFunctions::StringToTgeometry(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_string) -> string_t {
            std::string input_str = input_string.GetString();

            // Parse temporal geometry from string representation
            Temporal *temp = tgeometry_in(input_str.c_str());
            if (!temp) {
                throw InvalidInputException("Invalid TGEOMETRY input: " + input_str);
            }
            
            // Serialize to binary format
            size_t data_size = temporal_mem_size(temp);
            uint8_t *data_buffer = (uint8_t*)malloc(data_size);
            if (!data_buffer) {
                free(temp);
                throw InvalidInputException("Failed to allocate memory for TGEOMETRY data");
            }
            
            memcpy(data_buffer, temp, data_size);
            
            string_t data_string_t(reinterpret_cast<const char*>(data_buffer), data_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, data_string_t);
            
            free(data_buffer);
            free(temp);
            
            return stored_data;
        });
        
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
    return true;
}
```

This is what lets you write:
```sql
SELECT 'POINT(1 1)@2025-08-13 14:00+02'::TGEOMETRY;
```
The parser will call this function to turn the text into the internal binary format.

#### Custom Type to String
```cpp
bool TgeometryFunctions::TgeometryToString(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            
            // Validate binary data size
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("Invalid TGEOMETRY data: insufficient size");
            }

            // Create a copy of the binary data for safe manipulation
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            if (!data_copy) {
                throw InvalidInputException("Failed to allocate memory for TGEOMETRY deserialization");
            }
            memcpy(data_copy, data, data_size);
            
            // Cast to temporal object
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOMETRY data: null pointer");
            }
            
            // Convert to string representation
            char *str = temporal_out(temp, 15); // 15 = precision
            if (!str) {
                free(data_copy);
                throw InvalidInputException("Failed to convert TGEOMETRY to string");
            }
            
            std::string output(str);
            string_t stored_result = StringVector::AddString(result, output);
            
            free(str);
            free(data_copy);
            
            return stored_result;
        });
        
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
    return true;   
}
```

### 3. Utility Functions

Implement functions that operate on your custom type:

```cpp
inline void Temporal_start_instant(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_vec = args.data[0];

    UnaryExecutor::Execute<string_t, string_t>(
        input_vec, result, count,
        [&](string_t input_blob) -> string_t {
            // Note: This function expects binary data, not string
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            
            // Create a copy for safe manipulation
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            if (!data_copy) {
                throw InvalidInputException("Failed to allocate memory for temporal data");
            }
            memcpy(data_copy, data, data_size);

            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid temporal data");
            }

            // Extract the start instant from the temporal object
            TInstant *start_inst = temporal_start_instant(temp);
            if (!start_inst) {
                free(data_copy);
                throw InvalidInputException("Failed to get start instant from temporal object");
            }

            // Serialize the result
            size_t result_size = temporal_mem_size((Temporal*)start_inst);
            if (result_size == 0) {
                free(data_copy);
                throw InvalidInputException("Invalid result size from temporal object");
            }

            uint8_t *result_buffer = (uint8_t*)malloc(result_size);
            if (!result_buffer) {
                free(data_copy);
                throw InvalidInputException("Failed to allocate memory for result");
            }
            
            memcpy(result_buffer, start_inst, result_size);
            string_t result_string_t(reinterpret_cast<const char*>(result_buffer), result_size);
            string_t stored_result = StringVector::AddStringOrBlob(result, result_string_t);
            
            free(result_buffer);
            free(data_copy);
            
            return stored_result;
        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}
```

## III. Function Registration

### Register Cast Functions

Cast functions must be registered to enable automatic type conversions:

```cpp
void TGeometryTypes::RegisterCastFunctions(DatabaseInstance &instance) {
    // Register bidirectional casting between VARCHAR and TGEOMETRY
    ExtensionUtil::RegisterCastFunction(
        instance, 
        LogicalType::VARCHAR, 
        TGeometryTypes::TGEOMETRY(), 
        TgeometryFunctions::StringToTgeometry
    );
    
    ExtensionUtil::RegisterCastFunction(
        instance, 
        TGeometryTypes::TGEOMETRY(), 
        LogicalType::VARCHAR, 
        TgeometryFunctions::TgeometryToString
    );
}
```

### Register Scalar Functions

Register constructor and utility functions:

```cpp
void TGeometryTypes::RegisterScalarFunctions(DatabaseInstance &instance) {
    // Constructor function: TGEOMETRY(geometry_string, timestamp)
    auto tgeometry_constructor = ScalarFunction(
        "TGEOMETRY",                                           // Function name
        {LogicalType::VARCHAR, LogicalType::TIMESTAMP_TZ},     // Input parameter types
        TGeometryTypes::TGEOMETRY(),                           // Return type
        Tgeoinst_constructor                                   // Implementation function
    );
    ExtensionUtil::RegisterFunction(instance, tgeometry_constructor);
    
    // Utility function: start_instant(tgeometry)
    auto start_instant_function = ScalarFunction(
        "start_instant",                      // Function name
        {TGeometryTypes::TGEOMETRY()},        // Input parameter types
        TGeometryTypes::TGEOMETRY(),          // Return type
        Temporal_start_instant                // Implementation function
    );
    ExtensionUtil::RegisterFunction(instance, start_instant_function);
}
```

## IV. Complete Extension Setup

Tie everything together in your extension's main function:

```cpp
void TGeometryExtension::Load(DatabaseInstance &instance) {
    // Register the custom type
    TGeometryTypes::RegisterTypes(instance);
    
    // Register cast functions for type conversion
    TGeometryTypes::RegisterCastFunctions(instance);
    
    // Register scalar functions
    TGeometryTypes::RegisterScalarFunctions(instance);
}
```

## V. Usage Examples

Once registered, your custom type can be used in SQL
