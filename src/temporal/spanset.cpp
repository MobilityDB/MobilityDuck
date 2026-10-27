/*****************************************************************************
 *
 * This MobilityDB code is provided under The PostgreSQL License.
 * Copyright (c) 2016-2026, Université libre de Bruxelles and MobilityDB
 * contributors
 *
 * MobilityDB includes portions of PostGIS version 3 source code released
 * under the GNU General Public License (GPLv2 or later).
 * Copyright (c) 2001-2026, PostGIS contributors
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without a written
 * agreement is hereby granted, provided that the above copyright notice and
 * this paragraph and the following two paragraphs appear in all copies.
 *
 * IN NO EVENT SHALL UNIVERSITE LIBRE DE BRUXELLES BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
 * LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION,
 * EVEN IF UNIVERSITE LIBRE DE BRUXELLES HAS BEEN ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * UNIVERSITE LIBRE DE BRUXELLES SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON
 * AN "AS IS" BASIS, AND UNIVERSITE LIBRE DE BRUXELLES HAS NO OBLIGATIONS TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 *****************************************************************************/

#include "temporal/spanset.hpp"
#include "temporal/spanset_functions.hpp"
#include "temporal/span.hpp"
#include "temporal/set.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include "time_util.hpp"
#include "mobilityduck/meos_registration.hpp"


extern "C" {     
    #include "meos.h"    
    #include "meos_internal.h"   
    #include "meos_geo.h"
}

namespace duckdb {

// Collection type registration (accessors, RegisterTypes, AllTypes, alias->MeosType,
// GetChildType/GetSetType/GetBaseType) is generated from the catalog MeosType enum
// into src/generated/generated_type_registration.cpp.

// --- Register Cast ---
void SpansetTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    for (const auto &spanset_type : SpansetTypes::AllTypes()) {
        RegisterMeosCastFunction(loader, 
            spanset_type,                      
            LogicalType::VARCHAR,   
            SpansetFunctions::Spanset_to_text   
        ); // Blob to text
        RegisterMeosCastFunction(loader, 
            LogicalType::VARCHAR, 
            spanset_type,                                    
            SpansetFunctions::Text_to_spanset   
        ); // text to blob
        
        auto base_type = SpansetTypeMapping::GetBaseType(spanset_type);
        RegisterMeosCastFunction(loader, 
            base_type,
            spanset_type,
            SpansetFunctions::Value_to_spanset_cast
        );

        auto set_type = SpansetTypeMapping::GetSetType(spanset_type);        
        RegisterMeosCastFunction(loader, 
            set_type,
            spanset_type,
            SpansetFunctions::Set_to_spanset_cast
        );
        auto child_type = SpansetTypeMapping::GetChildType(spanset_type); // span
        RegisterMeosCastFunction(loader, 
            child_type,
            spanset_type,
            SpansetFunctions::Span_to_spanset_cast
        );

        RegisterMeosCastFunction(loader, 
            spanset_type,
            child_type,
            SpansetFunctions::Spanset_to_span_cast
        );

        RegisterMeosCastFunction(loader, 
            SpansetTypes::intspanset(),
            SpansetTypes::floatspanset(),
            SpansetFunctions::Intspanset_to_floatspanset_cast
        );

        RegisterMeosCastFunction(loader, 
            SpansetTypes::floatspanset(),
            SpansetTypes::intspanset(),
            SpansetFunctions::Floatspanset_to_intspanset_cast
        );

        RegisterMeosCastFunction(loader, 
            SpansetTypes::datespanset(),
            SpansetTypes::tstzspanset(),
            SpansetFunctions::Datespanset_to_tstzspanset_cast
        );

        RegisterMeosCastFunction(loader, 
            SpansetTypes::tstzspanset(),
            SpansetTypes::datespanset(),
            SpansetFunctions::Tstzspanset_to_datespanset_cast
        );
    }
}

} // namespace duckdb   
