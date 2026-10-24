/* Bounded first-hit sphere camera boom (AI-assisted proposal), ISC. */
#include "camera_boom.h"

#include <math.h>

static float dot3(dc_vec3 a, dc_vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static dc_vec3 mul3(dc_vec3 a, float s)
{
    dc_vec3 r;
    r.x = a.x * s;
    r.y = a.y * s;
    r.z = a.z * s;
    return r;
}

static dc_vec3 sub3(dc_vec3 a, dc_vec3 b)
{
    dc_vec3 r;
    r.x = a.x - b.x;
    r.y = a.y - b.y;
    r.z = a.z - b.z;
    return r;
}

static int finite3(dc_vec3 v)
{
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
}

/* Same ±4096 position domain the collision start check uses. */
static int in_domain(dc_vec3 v)
{
    return finite3(v)
        && fabsf(v.x) <= 4096.f
        && fabsf(v.y) <= 4096.f
        && fabsf(v.z) <= 4096.f;
}

static int direction_unit(dc_vec3 in, dc_vec3 *out)
{
    float len2, len;
    if (!finite3(in)) return 0;
    len2 = dot3(in, in);
    if (!isfinite(len2)) return 0;
    len = sqrtf(len2);
    if (!(fabsf(len - 1.f) <= DC_CAMERA_BOOM_DIRECTION_TOLERANCE)) return 0;
    *out = mul3(in, 1.f / len);
    return finite3(*out);
}

/* Along-boom distance to this cast stop, then the contact-shell inset.
 * The shorter of the projection, the chord, and the segment wins. */
static int contact_advance(dc_vec3 from, dc_vec3 to, dc_vec3 dir, float seg,
                           float *advance)
{
    dc_vec3 moved;
    float along, chord2, chord, step;
    moved = sub3(to, from);
    along = dot3(moved, dir);
    chord2 = dot3(moved, moved);
    if (!isfinite(along) || !isfinite(chord2)) return 0;
    chord = sqrtf(chord2);
    if (!isfinite(chord)) return 0;
    step = along;
    if (chord < step) step = chord;
    if (step > seg) step = seg;
    if (step < 0.f) step = 0.f;
    step -= DC_CAMERA_BOOM_CONTACT_MARGIN;
    if (step < 0.f) step = 0.f;
    *advance = step;
    return 1;
}

int dc_camera_boom_query(dc_collision_world *world, dc_vec3 target_center,
                         dc_vec3 direction, float desired_meters,
                         float *clear_meters)
{
    dc_vec3 dir, feet, pos;
    float remaining, covered;
    int segments;

    if (!clear_meters || !world || !world->ready) return 0;
    if (!in_domain(target_center)) return 0;
    if (!direction_unit(direction, &dir)) return 0;
    if (!(isfinite(desired_meters)
          && desired_meters >= DC_CAMERA_BOOM_MIN_METERS
          && desired_meters <= DC_CAMERA_BOOM_MAX_METERS))
        return 0;

    /* Feet are one radius below the center. Height is two radii, so the
     * capsule axis is that single center point. */
    feet = target_center;
    feet.y -= DC_CAMERA_BOOM_RADIUS;
    if (!in_domain(feet)) return 0;

    /* Overlap and candidate overflow refuse. No popped-out pose. */
    if (!dc_collision_clear(world, feet, DC_CAMERA_BOOM_RADIUS,
                            DC_CAMERA_BOOM_HEIGHT))
        return 0;

    pos = feet;
    remaining = desired_meters;
    covered = 0.f;
    segments = 0;
    while (remaining > 0.f && segments < 2) {
        float seg, d2, step;
        dc_vec3 delta;
        dc_collision_result hit = {0};

        if (!in_domain(pos)) return 0;
        seg = remaining;
        if (seg > DC_CAMERA_BOOM_SEGMENT_MAX) seg = DC_CAMERA_BOOM_SEGMENT_MAX;
        delta = mul3(dir, seg);
        d2 = dot3(delta, delta);
        /* Backstop for the cast's dot(displacement, displacement) > 64 test. */
        if (!(d2 <= 64.f)) return 0;
        /* CAST stops on the ray. MOVE would slide the leftover along a wall. */
        if (!dc_collision_cast(world, pos, delta, DC_CAMERA_BOOM_RADIUS,
                               DC_CAMERA_BOOM_HEIGHT, &hit))
            return 0;
        segments++;
        if (hit.contacts == 0) {
            if (!finite3(hit.position)) return 0;
            pos = hit.position;
            covered += seg;
            remaining -= seg;
            continue;
        }
        if (!contact_advance(pos, hit.position, dir, seg, &step)) return 0;
        covered += step;
        if (!isfinite(covered) || covered < 0.f) return 0;
        if (covered > desired_meters) covered = desired_meters;
        *clear_meters = covered;
        return 1;
    }
    if (!(remaining <= 0.f)) return 0;
    *clear_meters = desired_meters;
    return 1;
}
