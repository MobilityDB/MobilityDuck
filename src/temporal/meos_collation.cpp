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

/**
 * @file
 * @brief The order and the equality of the MEOS types in DuckDB
 *
 * DuckDB binds `=`, `<>`, `<`, `<=`, `>`, `>=`, BETWEEN, IN, ORDER BY,
 * GROUP BY, DISTINCT, window partitions and orders, joins and set operations
 * over a type stored as a BLOB to its comparison of the stored bytes, never to
 * a registered function (`bind_comparison_expression.cpp` of DuckDB v1.5.6).
 * The one hook it offers every such operation is a collation
 * (`CollationBinding`, as it orders TIME WITH TIME ZONE and INTERVAL): it
 * replaces the operand by an expression whose order and equality are the
 * type's. For a MEOS type that expression is the sort key MEOS gives the
 * value, bytes whose unsigned order is the type's comparison and whose
 * equality is its equality (doc/specs/meos-sort-keys.md of MobilityDB).
 *
 * The type is read from the name of the alias, which is its MEOS name, and
 * MEOS classifies it: a set, a span, a span set, a box and a temporal value
 * are keyed by their family, and a base value by the key of the set holding it
 * alone, a set key being one byte for the set type followed by the keys of its
 * values; a network segment and a Raquet tile, which no set holds, by their
 * own keys. A raster, whose equality is that of its serialized bytes
 * (#raster_eq of MEOS), is keyed by the bytes stored here: DuckDB compares them
 * as they are, so its equality is that of MEOS and PostGIS, and the order of
 * those bytes, which no reference states for a raster, has no meaning. A type
 * MEOS does not compare, the JSON path, for which PostgreSQL states neither an
 * order nor an equality, is refused by every such operation, MIN and MAX
 * included.
 */

#include "meos_wrapper_simple.hpp"
#include "mobilityduck/meos_collation.hpp"
#include "mobilityduck/meos_thread.hpp"

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/aggregate_function_catalog_entry.hpp"
#include "duckdb/catalog/catalog_entry/schema_catalog_entry.hpp"
#include "duckdb/catalog/catalog_entry/type_catalog_entry.hpp"
#include "duckdb/function/function_binder.hpp"
#include "duckdb/planner/expression_binder.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/planner/collation_binding.hpp"
#include "duckdb/planner/expression/bound_function_expression.hpp"

namespace duckdb {

namespace {

/* The kinds of key a MEOS type takes */
enum class MeosKeyKind { NONE, SET, SPAN, SPANSET, TBOX, STBOX, TPCBOX, TEMPORAL, BASE,
  NSEGMENT, RAQUET, IMAGE };

MeosKeyKind
meos_key_kind(MeosType type)
{
  if (set_type(type))
    return MeosKeyKind::SET;
  if (span_type(type))
    return MeosKeyKind::SPAN;
  if (spanset_type(type))
    return MeosKeyKind::SPANSET;
  if (type == T_TBOX)
    return MeosKeyKind::TBOX;
  if (type == T_STBOX)
    return MeosKeyKind::STBOX;
  if (type == T_TPCBOX)
    return MeosKeyKind::TPCBOX;
  if (temporal_type(type))
    return MeosKeyKind::TEMPORAL;
  if (set_basetype(type))
    return MeosKeyKind::BASE;
  /* The two types MEOS compares that no set holds key themselves */
  if (type == T_NSEGMENT)
    return MeosKeyKind::NSEGMENT;
  if (type == T_RAQUET)
    return MeosKeyKind::RAQUET;
  /* A raster is equal to another when their serialized bytes are the same
   * (#raster_eq of MEOS), which are the bytes stored here, so its stored bytes
   * are its key */
  if (type == T_RASTER)
    return MeosKeyKind::IMAGE;
  return MeosKeyKind::NONE;
}

/* The key of one stored value, allocated by MEOS */
uint8_t *
meos_value_sortkey(MeosKeyKind kind, MeosType type, const string_t &blob,
  size_t *size)
{
  /* The stored bytes are the memory image of the value, as the catalog's
   * sqlTypes state it, which MEOS reads aligned */
  size_t len = blob.GetSize();
  void *value = malloc(len);
  memcpy(value, blob.GetData(), len);
  uint8_t *result = nullptr;
  switch (kind)
  {
    case MeosKeyKind::SET:
      result = set_sortkey((const Set *) value, size);
      break;
    case MeosKeyKind::SPAN:
      result = span_sortkey((const Span *) value, size);
      break;
    case MeosKeyKind::SPANSET:
      result = spanset_sortkey((const SpanSet *) value, size);
      break;
    case MeosKeyKind::TBOX:
      result = tbox_sortkey((const TBox *) value, size);
      break;
    case MeosKeyKind::STBOX:
      result = stbox_sortkey((const STBox *) value, size);
      break;
    case MeosKeyKind::TPCBOX:
      result = tpcbox_sortkey((const TPCBox *) value, size);
      break;
    case MeosKeyKind::TEMPORAL:
      result = temporal_sortkey((const Temporal *) value, size);
      break;
    case MeosKeyKind::BASE:
    {
      /* A base value is ordered as the set holding it alone */
      Datum d = (Datum) value;
      Set *s = set_make(&d, 1, type, false);
      result = set_sortkey(s, size);
      free(s);
      break;
    }
    case MeosKeyKind::NSEGMENT:
      result = nsegment_sortkey((const Nsegment *) value, size);
      break;
    case MeosKeyKind::RAQUET:
      result = raquet_sortkey((const Raquet *) value, size);
      break;
    case MeosKeyKind::IMAGE:
    case MeosKeyKind::NONE:
      break;
  }
  free(value);
  return result;
}

bool
PushMeosCollation(ClientContext &context, unique_ptr<Expression> &source,
  const LogicalType &sql_type, CollationType)
{
  if (sql_type.id() != LogicalTypeId::BLOB || ! sql_type.HasAlias())
    return false;
  const string name = sql_type.GetAlias();
  MeosType type = meostype_from_string(name.c_str());
  if (type == T_UNKNOWN)
    return false;
  /* Every type MEOS compares has a key; a type it does not compare, the
   * PostGIS raster and the JSON path, has neither an order nor an equality,
   * and is refused, never compared by its stored bytes */
  MeosKeyKind kind = meos_key_kind(type);
  /* A raster keyed by its stored bytes is compared by DuckDB as it is */
  if (kind == MeosKeyKind::IMAGE)
    return false;
  if (kind == MeosKeyKind::NONE)
    throw NotImplementedException("Type %s has no order or equality: it "
      "cannot be compared, ordered, grouped or made distinct", name);

  ScalarFunction key_function("meos_sortkey", {sql_type}, LogicalType::BLOB,
    [kind, type](DataChunk &args, ExpressionState &, Vector &result) {
      EnsureMeosThreadInitialized();
      UnaryExecutor::Execute<string_t, string_t>(args.data[0], result,
        args.size(), [&](string_t blob) {
          size_t size;
          uint8_t *key = meos_value_sortkey(kind, type, blob, &size);
          if (! key)
            throw InvalidInputException("No sort key for a value of type %s",
              meostype_name(type));
          string_t out = StringVector::AddStringOrBlob(result,
            (const char *) key, size);
          free(key);
          return out;
        });
    });
  auto alias = source->GetAlias();
  vector<unique_ptr<Expression>> children;
  children.push_back(std::move(source));
  FunctionBinder function_binder(context);
  source = function_binder.BindScalarFunction(key_function,
    std::move(children));
  source->SetAlias(alias);
  return true;
}

/* Bind the aggregate of the given name over the arguments, as the binder
 * would, in place of a MEOS-typed overload: the replacement #BindMinMax of
 * DuckDB makes of a collated MIN by arg_min */
unique_ptr<FunctionData>
RebindAggregate(ClientContext &context, const string &name,
  AggregateFunction &function, vector<unique_ptr<Expression>> &arguments,
  bind_aggregate_function_t self)
{
  /* Looked up by its catalog type, not by the template GetEntry<T>, whose
   * use of the static T::Name an extension would define a second time */
  EntryLookupInfo lookup(CatalogType::AGGREGATE_FUNCTION_ENTRY, name);
  auto found = Catalog::GetEntry(context, "", "", lookup,
    OnEntryNotFound::RETURN_NULL);
  if (! found)
    throw NotImplementedException("Aggregate %s is not in the catalog", name);
  auto entry = &found->Cast<AggregateFunctionCatalogEntry>();
  vector<LogicalType> types;
  for (auto &arg : arguments)
    types.push_back(arg->return_type);
  FunctionBinder function_binder(context);
  ErrorData error;
  auto best = function_binder.BindFunction(entry->name, entry->functions,
    types, error);
  if (! best.IsValid())
    throw BinderException("No %s over the sort key of a MEOS value: %s", name,
      error.Message());
  function = entry->functions.GetFunctionByOffset(best.GetIndex());
  /* The key is a plain BLOB, which no MEOS-typed overload takes */
  if (function.GetBindCallback() == self)
    throw InternalException("%s over a sort key resolves to itself", name);
  if (function.HasBindCallback())
    return function.GetBindCallback()(context, function, arguments);
  return nullptr;
}

/* MIN and MAX read the value of least and greatest key, arg_min(x, x) and
 * arg_max(x, x), whose bind keys its second argument by the collation, as
 * #BindMinMax of DuckDB binds a collated VARCHAR */
unique_ptr<FunctionData>
BindMeosMinMax(ClientContext &context, AggregateFunction &function,
  vector<unique_ptr<Expression>> &arguments)
{
  const string name = function.name == "min" ? "arg_min" : "arg_max";
  arguments.push_back(arguments[0]->Copy());
  return RebindAggregate(context, name, function, arguments, BindMeosMinMax);
}

/* COUNT reads the key, so that COUNT(DISTINCT x) counts the values the type
 * holds distinct, a key being NULL exactly when its value is; the rebinding
 * is that of #BindMinMax of DuckDB */
unique_ptr<FunctionData>
BindMeosCount(ClientContext &context, AggregateFunction &function,
  vector<unique_ptr<Expression>> &arguments)
{
  auto type = arguments[0]->return_type;
  if (! ExpressionBinder::PushCollation(context, arguments[0], type))
    throw InternalException("No sort key for type %s", type.ToString());
  return RebindAggregate(context, "count", function, arguments,
    BindMeosCount);
}

} // namespace

/* The collation is registered as #CollationBinding registers that of TIME
 * WITH TIME ZONE (#PushTimeTZCollation) */
void
RegisterMeosCollation(ExtensionLoader &loader)
{
  auto &db = loader.GetDatabaseInstance();
  CollationBinding::Get(db).RegisterCollation(
    CollationCallback(PushMeosCollation));

  /* MIN, MAX and COUNT, which the collation does not reach, over each MEOS
   * type the extension registers as a BLOB. MIN and MAX read the key of a type
   * MEOS keys, and refuse a type it does not compare through the collation
   * their rebinding pushes; COUNT reads the key of a keyed type, so that
   * COUNT(DISTINCT x) counts the values it holds distinct. The overloads join
   * the sets of the built-in aggregates as #ExtensionLoader::AddFunctionOverload
   * joins a scalar overload to its set, an aggregate having no such loader
   * entry */
  auto transaction = CatalogTransaction::GetSystemTransaction(db);
  auto &schema = Catalog::GetSystemCatalog(db).GetSchema(transaction,
    DEFAULT_SCHEMA);
  auto aggregate = [&](const char *name) -> AggregateFunctionSet & {
    auto entry = schema.GetEntry(transaction,
      CatalogType::AGGREGATE_FUNCTION_ENTRY, name);
    if (! entry)
      throw InternalException("Aggregate %s is not in the catalog", name);
    return entry->Cast<AggregateFunctionCatalogEntry>().functions;
  };
  auto &min_set = aggregate("min");
  auto &max_set = aggregate("max");
  auto &count_set = aggregate("count");
  for (int i = 1; i < (int) NUM_MEOS_TYPES; i++)
  {
    MeosType type = (MeosType) i;
    const char *name = meostype_name(type);
    if (! name)
      continue;
    auto entry = schema.GetEntry(transaction, CatalogType::TYPE_ENTRY, name);
    if (! entry)
      continue;
    LogicalType sql_type = entry->Cast<TypeCatalogEntry>().user_type;
    if (sql_type.id() != LogicalTypeId::BLOB)
      continue;
    /* The built-in aggregates of DuckDB read a raster's stored bytes, its key */
    if (meos_key_kind(type) == MeosKeyKind::IMAGE)
      continue;
    min_set.AddFunction(AggregateFunction("min", {sql_type}, sql_type,
      nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, BindMeosMinMax));
    max_set.AddFunction(AggregateFunction("max", {sql_type}, sql_type,
      nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, BindMeosMinMax));
    if (meos_key_kind(type) == MeosKeyKind::NONE)
      continue;
    count_set.AddFunction(AggregateFunction("count", {sql_type},
      LogicalType::BIGINT, nullptr, nullptr, nullptr, nullptr, nullptr,
      nullptr, BindMeosCount));
  }
}

} // namespace duckdb
