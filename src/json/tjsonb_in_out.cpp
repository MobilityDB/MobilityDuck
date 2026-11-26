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

#include "json/tjsonb.hpp"
#include "temporal/temporal_blob.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include "mobilityduck/meos_registration.hpp"

extern "C" {
    #include <meos.h>
    #include <meos_internal.h>
}

/* meos_json.h requires pgtypes headers not installed in the public MEOS package.
 * Forward-declare the two entry points used by this translation unit. */
extern "C" {
    extern Temporal *tjsonb_in(const char *str);
}
/* The text of a tjsonb is written by temporal_out, the writer of every temporal value (meos.h):
 * MEOS reads a value per type and writes it per template. */

namespace duckdb {

void TJsonbTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    // Base jsonb value render cast (mirrors cbuffer()->VARCHAR): renders the
    // generated startValue/endValue jsonb base value as canonical JSON text.
    RegisterMeosCastFunction(loader, TJsonbTypes::jsonb(), LogicalType::VARCHAR,
                             TjsonbFunctions::Jsonb_out_cast);
    RegisterMeosCastFunction(loader, LogicalType::VARCHAR, TJsonbTypes::jsonb(),
                             TjsonbFunctions::Jsonb_in_cast);
    RegisterMeosCastFunction(loader, LogicalType::VARCHAR, TJsonbTypes::jsonpath(),
                             TjsonbFunctions::Jsonpath_in_cast);
    RegisterMeosCastFunction(loader, TJsonbTypes::jsonpath(), LogicalType::VARCHAR,
                             TjsonbFunctions::Jsonpath_out_cast);
}

} // namespace duckdb
