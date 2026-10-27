// Exact diagnostic extracts from ImageMap (original author: sentientstardust).
// See extracted_sources.json for provenance; not a replacement implementation.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>
namespace ImagemapProbe {
static constexpr double EPSILON = 1e-4;
struct TextureMappingOffsetWeightField {
    float min_x_mm { 0.f };
    float min_y_mm { 0.f };
    float bucket_width_mm { 1.f };
    float bucket_height_mm { 1.f };
    float sample_lookup_radius_mm { 0.f };
    int bucket_width { 0 };
    int bucket_height { 0 };
    size_t component_count { 0 };
    std::vector<float> sample_x_mm;
    std::vector<float> sample_y_mm;
    std::vector<float> sample_weight;
    std::vector<float> sample_r;
    std::vector<float> sample_g;
    std::vector<float> sample_b;
    std::vector<float> sample_normal_z;
    std::vector<uint8_t> sample_has_surface_normal_z;
    std::vector<float> sample_component_weights;
    std::vector<std::vector<uint32_t>> buckets;
    std::vector<float> fallback_weights;
    size_t invalid_normal_sample_count { 0 };
    bool raw_component_weights_from_texture { false };
    bool raw_top_surface_labels_from_texture { false };
    bool binary_dithered { false };

    bool empty() const
    {
        return bucket_width <= 0 ||
               bucket_height <= 0 ||
               component_count == 0 ||
               sample_x_mm.empty() ||
               sample_y_mm.size() != sample_x_mm.size() ||
               sample_weight.size() != sample_x_mm.size() ||
               sample_has_surface_normal_z.size() != sample_x_mm.size() ||
               sample_component_weights.size() != sample_x_mm.size() * component_count;
    }
};
float clamp01f(float v)
{
    if (!std::isfinite(v))
        return 0.f;
    return std::clamp(v, 0.f, 1.f);
}
std::vector<float> sample_weight_field_components_impl(const TextureMappingOffsetWeightField &weight_field,
                                                       float x_mm,
                                                       float y_mm,
                                                       bool high_resolution_texture_sampling)
{
    std::vector<float> fallback = weight_field.fallback_weights;
    if (fallback.size() < weight_field.component_count)
        fallback.resize(weight_field.component_count, 0.f);
    if (weight_field.empty() || !std::isfinite(x_mm) || !std::isfinite(y_mm))
        return fallback;

    const float gx = (x_mm - weight_field.min_x_mm) / std::max(weight_field.bucket_width_mm, 1e-6f);
    const float gy = (y_mm - weight_field.min_y_mm) / std::max(weight_field.bucket_height_mm, 1e-6f);
    const int cx = std::clamp(int(std::floor(gx)), 0, weight_field.bucket_width - 1);
    const int cy = std::clamp(int(std::floor(gy)), 0, weight_field.bucket_height - 1);
    if (weight_field.binary_dithered || weight_field.raw_top_surface_labels_from_texture) {
        float nearest_d2 = std::numeric_limits<float>::max();
        size_t nearest_sample_idx = size_t(-1);
        const int nearest_ring_limit = std::min(16, std::max(weight_field.bucket_width, weight_field.bucket_height));
        for (int ring = 0; ring <= nearest_ring_limit; ++ring) {
            const int min_x = std::max(0, cx - ring);
            const int max_x = std::min(weight_field.bucket_width - 1, cx + ring);
            const int min_y = std::max(0, cy - ring);
            const int max_y = std::min(weight_field.bucket_height - 1, cy + ring);

            auto visit_bucket = [&weight_field, x_mm, y_mm, &nearest_d2, &nearest_sample_idx](int bx, int by) {
                if (bx < 0 || by < 0 || bx >= weight_field.bucket_width || by >= weight_field.bucket_height)
                    return;
                const size_t bucket_idx = size_t(by) * size_t(weight_field.bucket_width) + size_t(bx);
                if (bucket_idx >= weight_field.buckets.size())
                    return;
                for (const uint32_t sample_idx_u32 : weight_field.buckets[bucket_idx]) {
                    const size_t sample_idx = size_t(sample_idx_u32);
                    if (sample_idx >= weight_field.sample_x_mm.size() || sample_idx >= weight_field.sample_y_mm.size())
                        continue;
                    const float dx = x_mm - weight_field.sample_x_mm[sample_idx];
                    const float dy = y_mm - weight_field.sample_y_mm[sample_idx];
                    const float d2 = dx * dx + dy * dy;
                    if (d2 >= nearest_d2)
                        continue;
                    const size_t value_idx = sample_idx * weight_field.component_count;
                    if (value_idx + weight_field.component_count > weight_field.sample_component_weights.size())
                        continue;
                    nearest_d2 = d2;
                    nearest_sample_idx = sample_idx;
                }
            };

            if (ring == 0) {
                visit_bucket(cx, cy);
            } else {
                for (int x = min_x; x <= max_x; ++x) {
                    visit_bucket(x, min_y);
                    if (max_y != min_y)
                        visit_bucket(x, max_y);
                }
                for (int y = min_y + 1; y <= max_y - 1; ++y) {
                    visit_bucket(min_x, y);
                    if (max_x != min_x)
                        visit_bucket(max_x, y);
                }
            }

            if (nearest_sample_idx != size_t(-1))
                break;
        }

        if (nearest_sample_idx != size_t(-1)) {
            std::vector<float> values(weight_field.component_count, 0.f);
            const size_t value_idx = nearest_sample_idx * weight_field.component_count;
            for (size_t component_idx = 0; component_idx < weight_field.component_count; ++component_idx)
                values[component_idx] = clamp01f(weight_field.sample_component_weights[value_idx + component_idx]);
            return values;
        }
        return fallback;
    }

    const bool has_lookup_radius_override =
        std::isfinite(weight_field.sample_lookup_radius_mm) && weight_field.sample_lookup_radius_mm > EPSILON;
    const float sigma_scale = high_resolution_texture_sampling ? 0.45f : 0.7f;
    const float min_sigma_mm = high_resolution_texture_sampling ? 0.04f : 0.06f;
    const float sigma_x_mm = has_lookup_radius_override ?
        std::max(0.01f, weight_field.sample_lookup_radius_mm * 0.4f) :
        std::max(min_sigma_mm, weight_field.bucket_width_mm * sigma_scale);
    const float sigma_y_mm = has_lookup_radius_override ?
        std::max(0.01f, weight_field.sample_lookup_radius_mm * 0.4f) :
        std::max(min_sigma_mm, weight_field.bucket_height_mm * sigma_scale);
    const float inv_two_sigma_x2 = 1.f / std::max(2.f * sigma_x_mm * sigma_x_mm, 1e-8f);
    const float inv_two_sigma_y2 = 1.f / std::max(2.f * sigma_y_mm * sigma_y_mm, 1e-8f);
    const float min_radius_mm = high_resolution_texture_sampling ? 0.16f : 0.30f;
    const float radius_scale = high_resolution_texture_sampling ? 1.75f : 3.f;
    const float max_radius_mm = has_lookup_radius_override ?
        weight_field.sample_lookup_radius_mm :
        std::max(min_radius_mm, std::max(weight_field.bucket_width_mm, weight_field.bucket_height_mm) * radius_scale);
    const float max_radius2 = max_radius_mm * max_radius_mm;
    const float min_bucket_span_mm = std::max(1e-3f, std::min(weight_field.bucket_width_mm, weight_field.bucket_height_mm));
    const int max_ring = std::max(1, int(std::ceil(max_radius_mm / min_bucket_span_mm)));

    std::vector<float> weighted_sum(weight_field.component_count, 0.f);
    float total_weight = 0.f;
    size_t contributing_samples = 0;
    auto process_bucket = [&](int bx, int by) {
        if (bx < 0 || by < 0 || bx >= weight_field.bucket_width || by >= weight_field.bucket_height)
            return;
        const size_t bucket_idx = size_t(by) * size_t(weight_field.bucket_width) + size_t(bx);
        if (bucket_idx >= weight_field.buckets.size())
            return;
        for (const uint32_t sample_idx_u32 : weight_field.buckets[bucket_idx]) {
            const size_t sample_idx = size_t(sample_idx_u32);
            if (sample_idx >= weight_field.sample_x_mm.size() ||
                sample_idx >= weight_field.sample_y_mm.size() ||
                sample_idx >= weight_field.sample_weight.size())
                continue;
            const float dx = x_mm - weight_field.sample_x_mm[sample_idx];
            const float dy = y_mm - weight_field.sample_y_mm[sample_idx];
            const float d2 = dx * dx + dy * dy;
            if (d2 > max_radius2)
                continue;
            const float kernel = std::exp(-(dx * dx) * inv_two_sigma_x2 - (dy * dy) * inv_two_sigma_y2);
            const float sample_w = weight_field.sample_weight[sample_idx] * kernel;
            if (!std::isfinite(sample_w) || sample_w <= EPSILON)
                continue;
            const size_t value_idx = sample_idx * weight_field.component_count;
            if (value_idx + weight_field.component_count > weight_field.sample_component_weights.size())
                continue;
            for (size_t component_idx = 0; component_idx < weight_field.component_count; ++component_idx)
                weighted_sum[component_idx] += weight_field.sample_component_weights[value_idx + component_idx] * sample_w;
            total_weight += sample_w;
            ++contributing_samples;
        }
    };

    for (int ring = 0; ring <= max_ring; ++ring) {
        const int min_x = std::max(0, cx - ring);
        const int max_x = std::min(weight_field.bucket_width - 1, cx + ring);
        const int min_y = std::max(0, cy - ring);
        const int max_y = std::min(weight_field.bucket_height - 1, cy + ring);
        if (ring == 0) {
            process_bucket(cx, cy);
        } else {
            for (int x = min_x; x <= max_x; ++x) {
                process_bucket(x, min_y);
                if (max_y != min_y)
                    process_bucket(x, max_y);
            }
            for (int y = min_y + 1; y <= max_y - 1; ++y) {
                process_bucket(min_x, y);
                if (max_x != min_x)
                    process_bucket(max_x, y);
            }
        }
        if (total_weight > EPSILON && contributing_samples >= 12)
            break;
    }

    if (total_weight > EPSILON) {
        std::vector<float> values(weight_field.component_count, 0.f);
        for (size_t component_idx = 0; component_idx < weight_field.component_count; ++component_idx)
            values[component_idx] = clamp01f(weighted_sum[component_idx] / total_weight);
        return values;
    }

    float nearest_d2 = std::numeric_limits<float>::max();
    size_t nearest_sample_idx = size_t(-1);
    const int nearest_ring_limit = std::min(std::max(max_ring + 2, 4), std::max(weight_field.bucket_width, weight_field.bucket_height));
    for (int ring = 0; ring <= nearest_ring_limit; ++ring) {
        const int min_x = std::max(0, cx - ring);
        const int max_x = std::min(weight_field.bucket_width - 1, cx + ring);
        const int min_y = std::max(0, cy - ring);
        const int max_y = std::min(weight_field.bucket_height - 1, cy + ring);

        auto visit_bucket = [&weight_field, x_mm, y_mm, &nearest_d2, &nearest_sample_idx](int bx, int by) {
            if (bx < 0 || by < 0 || bx >= weight_field.bucket_width || by >= weight_field.bucket_height)
                return;
            const size_t bucket_idx = size_t(by) * size_t(weight_field.bucket_width) + size_t(bx);
            if (bucket_idx >= weight_field.buckets.size())
                return;
            for (const uint32_t sample_idx_u32 : weight_field.buckets[bucket_idx]) {
                const size_t sample_idx = size_t(sample_idx_u32);
                if (sample_idx >= weight_field.sample_x_mm.size() || sample_idx >= weight_field.sample_y_mm.size())
                    continue;
                const float dx = x_mm - weight_field.sample_x_mm[sample_idx];
                const float dy = y_mm - weight_field.sample_y_mm[sample_idx];
                const float d2 = dx * dx + dy * dy;
                if (d2 >= nearest_d2)
                    continue;
                const size_t value_idx = sample_idx * weight_field.component_count;
                if (value_idx + weight_field.component_count > weight_field.sample_component_weights.size())
                    continue;
                nearest_d2 = d2;
                nearest_sample_idx = sample_idx;
            }
        };

        if (ring == 0) {
            visit_bucket(cx, cy);
        } else {
            for (int x = min_x; x <= max_x; ++x) {
                visit_bucket(x, min_y);
                if (max_y != min_y)
                    visit_bucket(x, max_y);
            }
            for (int y = min_y + 1; y <= max_y - 1; ++y) {
                visit_bucket(min_x, y);
                if (max_x != min_x)
                    visit_bucket(max_x, y);
            }
        }
        if (nearest_d2 < std::numeric_limits<float>::max() && ring >= 2)
            break;
    }

    if (nearest_sample_idx != size_t(-1)) {
        std::vector<float> values(weight_field.component_count, 0.f);
        const size_t value_idx = nearest_sample_idx * weight_field.component_count;
        for (size_t component_idx = 0; component_idx < weight_field.component_count; ++component_idx)
            values[component_idx] = clamp01f(weight_field.sample_component_weights[value_idx + component_idx]);
        return values;
    }
    return fallback;
}
}
