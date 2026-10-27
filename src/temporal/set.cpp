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

#include "temporal/set.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include "duckdb/function/function_set.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include "time_util.hpp"
#include "mobilityduck/meos_registration.hpp"

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

} // namespace duckdb
