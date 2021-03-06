/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/trace_processor/dynamic/experimental_slice_layout_generator.h"

#include <algorithm>

#include "test/gtest_and_gmock.h"

namespace perfetto {
namespace trace_processor {
namespace {

constexpr uint32_t kColumn =
    ExperimentalSliceLayoutGenerator::kFilterTrackIdsColumnIndex;

std::string ToVis(const Table& table) {
  const Column* layout_depth_column = table.GetColumnByName("layout_depth");
  const Column* ts_column = table.GetColumnByName("ts");
  const Column* dur_column = table.GetColumnByName("dur");
  const Column* filter_track_ids_column =
      table.GetColumnByName("filter_track_ids");

  std::vector<std::string> lines;
  for (uint32_t i = 0; i < table.row_count(); ++i) {
    int64_t layout_depth = layout_depth_column->Get(i).long_value;
    int64_t ts = ts_column->Get(i).long_value;
    int64_t dur = dur_column->Get(i).long_value;
    const char* filter_track_ids = filter_track_ids_column->Get(i).AsString();
    if (std::string("") == filter_track_ids) {
      continue;
    }
    for (int64_t j = 0; j < dur; ++j) {
      size_t y = static_cast<size_t>(layout_depth);
      size_t x = static_cast<size_t>(ts + j);
      while (lines.size() <= y) {
        lines.push_back("");
      }
      if (lines[y].size() <= x) {
        lines[y].resize(x + 1, ' ');
      }
      lines[y][x] = '#';
    }
  }

  std::string output = "";
  output += "\n";
  for (const std::string& line : lines) {
    output += line;
    output += "\n";
  }
  return output;
}

void ExpectOutput(const Table& table, const std::string& expected) {
  const auto& actual = ToVis(table);
  EXPECT_EQ(actual, expected)
      << "Actual:" << actual << "\nExpected:" << expected;
}

tables::SliceTable::Row SliceRow(int64_t ts,
                                 int64_t dur,
                                 uint32_t depth,
                                 uint32_t track_id,
                                 StringId name,
                                 int64_t stack_id = 0,
                                 int64_t parent_stack_id = 0) {
  tables::SliceTable::Row row;
  row.ts = ts;
  row.dur = dur;
  row.depth = depth;
  row.track_id = tables::TrackTable::Id{track_id};
  row.name = name;
  row.stack_id = stack_id;
  row.parent_stack_id = parent_stack_id;
  return row;
}

TEST(ExperimentalSliceLayoutGeneratorTest, SingleRow) {
  StringPool pool;
  tables::SliceTable slice_table(&pool, nullptr);
  StringId name = pool.InternString("SingleRow");

  slice_table.Insert(SliceRow(1 /*ts*/, 5 /*dur*/, 0u /*depth*/, 1u /*id*/,
                              name /*slice_name*/));

  ExperimentalSliceLayoutGenerator gen(&pool, &slice_table);

  std::unique_ptr<Table> table = gen.ComputeTable(
      {Constraint{kColumn, FilterOp::kEq, SqlValue::String("1")}}, {});
  ExpectOutput(*table, R"(
 #####
)");
}

TEST(ExperimentalSliceLayoutGeneratorTest, MultipleRows) {
  StringPool pool;
  tables::SliceTable slice_table(&pool, nullptr);
  StringId name = pool.InternString("MultipleRows");

  slice_table.Insert(SliceRow(1 /*ts*/, 5 /*dur*/, 0u /*depth*/, 1u /*id*/,
                              name /*slice_name*/));
  slice_table.Insert(SliceRow(1 /*ts*/, 4 /*dur*/, 1u /*depth*/, 1u /*id*/,
                              name /*slice_name*/));
  slice_table.Insert(SliceRow(1 /*ts*/, 3 /*dur*/, 2u /*depth*/, 1u /*id*/,
                              name /*slice_name*/));
  slice_table.Insert(SliceRow(1 /*ts*/, 2 /*dur*/, 3u /*depth*/, 1u /*id*/,
                              name /*slice_name*/));
  slice_table.Insert(SliceRow(1 /*ts*/, 1 /*dur*/, 4u /*depth*/, 1u /*id*/,
                              name /*slice_name*/));

  ExperimentalSliceLayoutGenerator gen(&pool, &slice_table);

  std::unique_ptr<Table> table = gen.ComputeTable(
      {Constraint{kColumn, FilterOp::kEq, SqlValue::String("1")}}, {});
  ExpectOutput(*table, R"(
 #####
 ####
 ###
 ##
 #
)");
}

TEST(ExperimentalSliceLayoutGeneratorTest, MultipleTracks) {
  StringPool pool;
  tables::SliceTable slice_table(&pool, nullptr);
  StringId name1 = pool.InternString("Slice1");
  StringId name2 = pool.InternString("Slice2");
  StringId name3 = pool.InternString("Slice3");
  StringId name4 = pool.InternString("Track4");

  slice_table.Insert(SliceRow(0 /*ts*/, 4 /*dur*/, 0u /*depth*/, 1u /*id*/,
                              name1 /*slice_name*/, 1 /*stack_id*/));
  slice_table.Insert(SliceRow(0 /*ts*/, 2 /*dur*/, 1u /*depth*/, 1u /*id*/,
                              name2 /*slice_name*/, 2 /*stack_id*/,
                              1 /*parent_stack_id*/));
  slice_table.Insert(SliceRow(3 /*ts*/, 4 /*dur*/, 0u /*depth*/, 2u /*id*/,
                              name3 /*slice_name*/, 3 /*stack_id*/));
  slice_table.Insert(SliceRow(3 /*ts*/, 2 /*dur*/, 1u /*depth*/, 2u /*id*/,
                              name4 /*slice_name*/, 4 /*stack_id*/,
                              3 /*parent_stack_id*/));

  ExperimentalSliceLayoutGenerator gen(&pool, &slice_table);

  std::unique_ptr<Table> table = gen.ComputeTable(
      {Constraint{kColumn, FilterOp::kEq, SqlValue::String("1,2")}}, {});
  ExpectOutput(*table, R"(
####
##
   ####
   ##
)");
}

TEST(ExperimentalSliceLayoutGeneratorTest, MultipleTracksWithGap) {
  StringPool pool;
  tables::SliceTable slice_table(&pool, nullptr);
  StringId name1 = pool.InternString("Slice1");
  StringId name2 = pool.InternString("Slice2");
  StringId name3 = pool.InternString("Slice3");
  StringId name4 = pool.InternString("Slice4");
  StringId name5 = pool.InternString("Slice5");
  StringId name6 = pool.InternString("Slice6");

  slice_table.Insert(SliceRow(0 /*ts*/, 4 /*dur*/, 0u /*depth*/, 1u /*id*/,
                              name1 /*slice_name*/, 1 /*stack_id*/));
  slice_table.Insert(SliceRow(0 /*ts*/, 2 /*dur*/, 1u /*depth*/, 1u /*id*/,
                              name2 /*slice_name*/, 2 /*stack_id*/,
                              1 /*parent_stack_id*/));
  slice_table.Insert(SliceRow(3 /*ts*/, 4 /*dur*/, 0u /*depth*/, 2u /*id*/,
                              name3 /*slice_name*/, 3 /*stack_id*/));
  slice_table.Insert(SliceRow(3 /*ts*/, 2 /*dur*/, 1u /*depth*/, 2u /*id*/,
                              name4 /*slice_name*/, 4 /*stack_id*/,
                              3 /*parent_stack_id*/));
  slice_table.Insert(SliceRow(5 /*ts*/, 4 /*dur*/, 0u /*depth*/, 1u /*id*/,
                              name5 /*slice_name*/, 5 /*stack_id*/));
  slice_table.Insert(SliceRow(5 /*ts*/, 2 /*dur*/, 1u /*depth*/, 1u /*id*/,
                              name6 /*slice_name*/, 6 /*stack_id*/,
                              5 /*parent_stack_id*/));

  ExperimentalSliceLayoutGenerator gen(&pool, &slice_table);

  std::unique_ptr<Table> table = gen.ComputeTable(
      {Constraint{kColumn, FilterOp::kEq, SqlValue::String("1,2,3")}}, {});
  ExpectOutput(*table, R"(
#### ####
##   ##
   ####
   ##
)");
}

TEST(ExperimentalSliceLayoutGeneratorTest, FilterOutTracks) {
  StringPool pool;
  tables::SliceTable slice_table(&pool, nullptr);
  StringId name1 = pool.InternString("Slice1");
  StringId name2 = pool.InternString("Slice2");
  StringId name3 = pool.InternString("Slice3");
  StringId name4 = pool.InternString("Slice4");
  StringId name5 = pool.InternString("Slice5");

  slice_table.Insert(SliceRow(0 /*ts*/, 4 /*dur*/, 0u /*depth*/, 1u /*id*/,
                              name1 /*slice_name*/, 1 /*stack_id*/));
  slice_table.Insert(SliceRow(0 /*ts*/, 2 /*dur*/, 1u /*depth*/, 1u /*id*/,
                              name2 /*slice_name*/, 2 /*stack_id*/,
                              1 /*parent_stack_id*/));
  slice_table.Insert(SliceRow(3 /*ts*/, 4 /*dur*/, 0u /*depth*/, 2u /*id*/,
                              name3 /*slice_name*/, 3 /*stack_id*/));
  slice_table.Insert(SliceRow(3 /*ts*/, 2 /*dur*/, 1u /*depth*/, 2u /*id*/,
                              name4 /*slice_name*/, 4 /*stack_id*/,
                              3 /*parent_stack_id*/));
  // This slice should be ignored as it's not in the filter below:
  slice_table.Insert(SliceRow(0 /*ts*/, 9 /*dur*/, 0u /*depth*/, 3u /*id*/,
                              name5 /*slice_name*/, 5 /*stack_id*/));

  ExperimentalSliceLayoutGenerator gen(&pool, &slice_table);
  std::unique_ptr<Table> table = gen.ComputeTable(
      {Constraint{kColumn, FilterOp::kEq, SqlValue::String("1,2")}}, {});
  ExpectOutput(*table, R"(
####
##
   ####
   ##
)");
}

}  // namespace
}  // namespace trace_processor
}  // namespace perfetto
