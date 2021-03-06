/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <gtest/gtest.h>
#include <liblp/builder.h>

using namespace std;
using namespace android::fs_mgr;

static const char* TEST_GUID = "A799D1D6-669F-41D8-A3F0-EBB7572D8302";
static const char* TEST_GUID2 = "A799D1D6-669F-41D8-A3F0-EBB7572D8303";

TEST(liblp, BuildBasic) {
    unique_ptr<MetadataBuilder> builder = MetadataBuilder::New(1024 * 1024, 1024, 2);

    Partition* partition = builder->AddPartition("system", TEST_GUID, LP_PARTITION_ATTR_READONLY);
    ASSERT_NE(partition, nullptr);
    EXPECT_EQ(partition->name(), "system");
    EXPECT_EQ(partition->guid(), TEST_GUID);
    EXPECT_EQ(partition->attributes(), LP_PARTITION_ATTR_READONLY);
    EXPECT_EQ(partition->size(), 0);
    EXPECT_EQ(builder->FindPartition("system"), partition);

    builder->RemovePartition("system");
    EXPECT_EQ(builder->FindPartition("system"), nullptr);
}

TEST(liblp, ResizePartition) {
    unique_ptr<MetadataBuilder> builder = MetadataBuilder::New(1024 * 1024, 1024, 2);

    Partition* system = builder->AddPartition("system", TEST_GUID, LP_PARTITION_ATTR_READONLY);
    ASSERT_NE(system, nullptr);
    EXPECT_EQ(builder->GrowPartition(system, 65536), true);
    EXPECT_EQ(system->size(), 65536);
    ASSERT_EQ(system->extents().size(), 1);

    LinearExtent* extent = system->extents()[0]->AsLinearExtent();
    ASSERT_NE(extent, nullptr);
    EXPECT_EQ(extent->num_sectors(), 65536 / LP_SECTOR_SIZE);
    // The first logical sector will be (4096+1024*2)/512 = 12.
    EXPECT_EQ(extent->physical_sector(), 12);

    // Test growing to the same size.
    EXPECT_EQ(builder->GrowPartition(system, 65536), true);
    EXPECT_EQ(system->size(), 65536);
    EXPECT_EQ(system->extents().size(), 1);
    EXPECT_EQ(system->extents()[0]->num_sectors(), 65536 / LP_SECTOR_SIZE);
    // Test growing to a smaller size.
    EXPECT_EQ(builder->GrowPartition(system, 0), true);
    EXPECT_EQ(system->size(), 65536);
    EXPECT_EQ(system->extents().size(), 1);
    EXPECT_EQ(system->extents()[0]->num_sectors(), 65536 / LP_SECTOR_SIZE);
    // Test shrinking to a greater size.
    builder->ShrinkPartition(system, 131072);
    EXPECT_EQ(system->size(), 65536);
    EXPECT_EQ(system->extents().size(), 1);
    EXPECT_EQ(system->extents()[0]->num_sectors(), 65536 / LP_SECTOR_SIZE);

    // Test shrinking within the same extent.
    builder->ShrinkPartition(system, 32768);
    EXPECT_EQ(system->size(), 32768);
    EXPECT_EQ(system->extents().size(), 1);
    extent = system->extents()[0]->AsLinearExtent();
    ASSERT_NE(extent, nullptr);
    EXPECT_EQ(extent->num_sectors(), 32768 / LP_SECTOR_SIZE);
    EXPECT_EQ(extent->physical_sector(), 12);

    // Test shrinking to 0.
    builder->ShrinkPartition(system, 0);
    EXPECT_EQ(system->size(), 0);
    EXPECT_EQ(system->extents().size(), 0);
}

TEST(liblp, PartitionAlignment) {
    unique_ptr<MetadataBuilder> builder = MetadataBuilder::New(1024 * 1024, 1024, 2);

    // Test that we align up to one sector.
    Partition* system = builder->AddPartition("system", TEST_GUID, LP_PARTITION_ATTR_READONLY);
    ASSERT_NE(system, nullptr);
    EXPECT_EQ(builder->GrowPartition(system, 10000), true);
    EXPECT_EQ(system->size(), 10240);
    EXPECT_EQ(system->extents().size(), 1);

    builder->ShrinkPartition(system, 9000);
    EXPECT_EQ(system->size(), 9216);
    EXPECT_EQ(system->extents().size(), 1);
}

TEST(liblp, DiskAlignment) {
    static const uint64_t kDiskSize = 1000000;
    static const uint32_t kMetadataSize = 1024;
    static const uint32_t kMetadataSlots = 2;

    // If the disk size is not aligned to 512 bytes, make sure it still leaves
    // space at the end for backup metadata, and that it doesn't overlap with
    // the space for logical partitions.
    unique_ptr<MetadataBuilder> builder =
            MetadataBuilder::New(kDiskSize, kMetadataSize, kMetadataSlots);
    unique_ptr<LpMetadata> exported = builder->Export();
    ASSERT_NE(exported, nullptr);

    static const size_t kMetadataSpace =
            (kMetadataSize * kMetadataSlots) + LP_METADATA_GEOMETRY_SIZE;
    uint64_t space_at_end =
            kDiskSize - (exported->geometry.last_logical_sector + 1) * LP_SECTOR_SIZE;
    EXPECT_GE(space_at_end, kMetadataSpace);
}

TEST(liblp, MetadataAlignment) {
    // Make sure metadata sizes get aligned up.
    unique_ptr<MetadataBuilder> builder = MetadataBuilder::New(1024 * 1024, 1000, 2);
    unique_ptr<LpMetadata> exported = builder->Export();
    ASSERT_NE(exported, nullptr);
    EXPECT_EQ(exported->geometry.metadata_max_size, 1024);
}

TEST(liblp, UseAllDiskSpace) {
    unique_ptr<MetadataBuilder> builder = MetadataBuilder::New(1024 * 1024, 1024, 2);
    EXPECT_EQ(builder->AllocatableSpace(), 1036288);

    Partition* system = builder->AddPartition("system", TEST_GUID, LP_PARTITION_ATTR_READONLY);
    ASSERT_NE(system, nullptr);
    EXPECT_EQ(builder->GrowPartition(system, 1036288), true);
    EXPECT_EQ(system->size(), 1036288);
    EXPECT_EQ(builder->GrowPartition(system, 1036289), false);
}

TEST(liblp, BuildComplex) {
    unique_ptr<MetadataBuilder> builder = MetadataBuilder::New(1024 * 1024, 1024, 2);

    Partition* system = builder->AddPartition("system", TEST_GUID, LP_PARTITION_ATTR_READONLY);
    Partition* vendor = builder->AddPartition("vendor", TEST_GUID2, LP_PARTITION_ATTR_READONLY);
    ASSERT_NE(system, nullptr);
    ASSERT_NE(vendor, nullptr);
    EXPECT_EQ(builder->GrowPartition(system, 65536), true);
    EXPECT_EQ(builder->GrowPartition(vendor, 32768), true);
    EXPECT_EQ(builder->GrowPartition(system, 98304), true);
    EXPECT_EQ(system->size(), 98304);
    EXPECT_EQ(vendor->size(), 32768);

    // We now expect to have 3 extents total: 2 for system, 1 for vendor, since
    // our allocation strategy is greedy/first-fit.
    ASSERT_EQ(system->extents().size(), 2);
    ASSERT_EQ(vendor->extents().size(), 1);

    LinearExtent* system1 = system->extents()[0]->AsLinearExtent();
    LinearExtent* system2 = system->extents()[1]->AsLinearExtent();
    LinearExtent* vendor1 = vendor->extents()[0]->AsLinearExtent();
    ASSERT_NE(system1, nullptr);
    ASSERT_NE(system2, nullptr);
    ASSERT_NE(vendor1, nullptr);
    EXPECT_EQ(system1->num_sectors(), 65536 / LP_SECTOR_SIZE);
    EXPECT_EQ(system1->physical_sector(), 12);
    EXPECT_EQ(system2->num_sectors(), 32768 / LP_SECTOR_SIZE);
    EXPECT_EQ(system2->physical_sector(), 204);
    EXPECT_EQ(vendor1->num_sectors(), 32768 / LP_SECTOR_SIZE);
    EXPECT_EQ(vendor1->physical_sector(), 140);
    EXPECT_EQ(system1->physical_sector() + system1->num_sectors(), vendor1->physical_sector());
    EXPECT_EQ(vendor1->physical_sector() + vendor1->num_sectors(), system2->physical_sector());
}

TEST(liblp, AddInvalidPartition) {
    unique_ptr<MetadataBuilder> builder = MetadataBuilder::New(1024 * 1024, 1024, 2);

    Partition* partition = builder->AddPartition("system", TEST_GUID, LP_PARTITION_ATTR_READONLY);
    ASSERT_NE(partition, nullptr);

    // Duplicate name.
    partition = builder->AddPartition("system", TEST_GUID, LP_PARTITION_ATTR_READONLY);
    EXPECT_EQ(partition, nullptr);

    // Empty name.
    partition = builder->AddPartition("", TEST_GUID, LP_PARTITION_ATTR_READONLY);
    EXPECT_EQ(partition, nullptr);
}

TEST(liblp, BuilderExport) {
    static const uint64_t kDiskSize = 1024 * 1024;
    static const uint32_t kMetadataSize = 1024;
    static const uint32_t kMetadataSlots = 2;
    unique_ptr<MetadataBuilder> builder =
            MetadataBuilder::New(kDiskSize, kMetadataSize, kMetadataSlots);

    Partition* system = builder->AddPartition("system", TEST_GUID, LP_PARTITION_ATTR_READONLY);
    Partition* vendor = builder->AddPartition("vendor", TEST_GUID2, LP_PARTITION_ATTR_READONLY);
    ASSERT_NE(system, nullptr);
    ASSERT_NE(vendor, nullptr);
    EXPECT_EQ(builder->GrowPartition(system, 65536), true);
    EXPECT_EQ(builder->GrowPartition(vendor, 32768), true);
    EXPECT_EQ(builder->GrowPartition(system, 98304), true);

    unique_ptr<LpMetadata> exported = builder->Export();
    EXPECT_NE(exported, nullptr);

    // Verify geometry. Some details of this may change if we change the
    // metadata structures. So in addition to checking the exact values, we
    // also check that they are internally consistent after.
    const LpMetadataGeometry& geometry = exported->geometry;
    EXPECT_EQ(geometry.magic, LP_METADATA_GEOMETRY_MAGIC);
    EXPECT_EQ(geometry.struct_size, sizeof(geometry));
    EXPECT_EQ(geometry.metadata_max_size, 1024);
    EXPECT_EQ(geometry.metadata_slot_count, 2);
    EXPECT_EQ(geometry.first_logical_sector, 12);
    EXPECT_EQ(geometry.last_logical_sector, 2035);

    static const size_t kMetadataSpace =
            (kMetadataSize * kMetadataSlots) + LP_METADATA_GEOMETRY_SIZE;
    uint64_t space_at_end = kDiskSize - (geometry.last_logical_sector + 1) * LP_SECTOR_SIZE;
    EXPECT_GE(space_at_end, kMetadataSpace);
    EXPECT_GE(geometry.first_logical_sector * LP_SECTOR_SIZE, kMetadataSpace);

    // Verify header.
    const LpMetadataHeader& header = exported->header;
    EXPECT_EQ(header.magic, LP_METADATA_HEADER_MAGIC);
    EXPECT_EQ(header.major_version, LP_METADATA_MAJOR_VERSION);
    EXPECT_EQ(header.minor_version, LP_METADATA_MINOR_VERSION);

    ASSERT_EQ(exported->partitions.size(), 2);
    ASSERT_EQ(exported->extents.size(), 3);

    for (const auto& partition : exported->partitions) {
        Partition* original = builder->FindPartition(GetPartitionName(partition));
        ASSERT_NE(original, nullptr);
        EXPECT_EQ(original->guid(), GetPartitionGuid(partition));
        for (size_t i = 0; i < partition.num_extents; i++) {
            const auto& extent = exported->extents[partition.first_extent_index + i];
            LinearExtent* original_extent = original->extents()[i]->AsLinearExtent();
            EXPECT_EQ(extent.num_sectors, original_extent->num_sectors());
            EXPECT_EQ(extent.target_type, LP_TARGET_TYPE_LINEAR);
            EXPECT_EQ(extent.target_data, original_extent->physical_sector());
        }
        EXPECT_EQ(partition.attributes, original->attributes());
    }
}

TEST(liblp, BuilderImport) {
    unique_ptr<MetadataBuilder> builder = MetadataBuilder::New(1024 * 1024, 1024, 2);

    Partition* system = builder->AddPartition("system", TEST_GUID, LP_PARTITION_ATTR_READONLY);
    Partition* vendor = builder->AddPartition("vendor", TEST_GUID2, LP_PARTITION_ATTR_READONLY);
    ASSERT_NE(system, nullptr);
    ASSERT_NE(vendor, nullptr);
    EXPECT_EQ(builder->GrowPartition(system, 65536), true);
    EXPECT_EQ(builder->GrowPartition(vendor, 32768), true);
    EXPECT_EQ(builder->GrowPartition(system, 98304), true);

    unique_ptr<LpMetadata> exported = builder->Export();
    ASSERT_NE(exported, nullptr);

    builder = MetadataBuilder::New(*exported.get());
    ASSERT_NE(builder, nullptr);
    system = builder->FindPartition("system");
    ASSERT_NE(system, nullptr);
    vendor = builder->FindPartition("vendor");
    ASSERT_NE(vendor, nullptr);

    EXPECT_EQ(system->size(), 98304);
    ASSERT_EQ(system->extents().size(), 2);
    EXPECT_EQ(system->guid(), TEST_GUID);
    EXPECT_EQ(system->attributes(), LP_PARTITION_ATTR_READONLY);
    EXPECT_EQ(vendor->size(), 32768);
    ASSERT_EQ(vendor->extents().size(), 1);
    EXPECT_EQ(vendor->guid(), TEST_GUID2);
    EXPECT_EQ(vendor->attributes(), LP_PARTITION_ATTR_READONLY);

    LinearExtent* system1 = system->extents()[0]->AsLinearExtent();
    LinearExtent* system2 = system->extents()[1]->AsLinearExtent();
    LinearExtent* vendor1 = vendor->extents()[0]->AsLinearExtent();
    EXPECT_EQ(system1->num_sectors(), 65536 / LP_SECTOR_SIZE);
    EXPECT_EQ(system1->physical_sector(), 12);
    EXPECT_EQ(system2->num_sectors(), 32768 / LP_SECTOR_SIZE);
    EXPECT_EQ(system2->physical_sector(), 204);
    EXPECT_EQ(vendor1->num_sectors(), 32768 / LP_SECTOR_SIZE);
}

TEST(liblp, ExportNameTooLong) {
    unique_ptr<MetadataBuilder> builder = MetadataBuilder::New(1024 * 1024, 1024, 2);

    std::string name = "abcdefghijklmnopqrstuvwxyz0123456789";
    Partition* system = builder->AddPartition(name + name, TEST_GUID, LP_PARTITION_ATTR_READONLY);
    EXPECT_NE(system, nullptr);

    unique_ptr<LpMetadata> exported = builder->Export();
    EXPECT_EQ(exported, nullptr);
}

TEST(liblp, ExportInvalidGuid) {
    unique_ptr<MetadataBuilder> builder = MetadataBuilder::New(1024 * 1024, 1024, 2);

    Partition* system = builder->AddPartition("system", "bad", LP_PARTITION_ATTR_READONLY);
    EXPECT_NE(system, nullptr);

    unique_ptr<LpMetadata> exported = builder->Export();
    EXPECT_EQ(exported, nullptr);
}

TEST(liblp, MetadataTooLarge) {
    static const size_t kDiskSize = 128 * 1024;
    static const size_t kMetadataSize = 64 * 1024;

    // No space to store metadata + geometry.
    unique_ptr<MetadataBuilder> builder = MetadataBuilder::New(kDiskSize, kMetadataSize, 1);
    EXPECT_EQ(builder, nullptr);

    // No space to store metadata + geometry + one free sector.
    builder = MetadataBuilder::New(kDiskSize + LP_METADATA_GEOMETRY_SIZE * 2, kMetadataSize, 1);
    EXPECT_EQ(builder, nullptr);

    // Space for metadata + geometry + one free sector.
    builder = MetadataBuilder::New(kDiskSize + LP_METADATA_GEOMETRY_SIZE * 2 + LP_SECTOR_SIZE,
                                   kMetadataSize, 1);
    EXPECT_NE(builder, nullptr);
}
