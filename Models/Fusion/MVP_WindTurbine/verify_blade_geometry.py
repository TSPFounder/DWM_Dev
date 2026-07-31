"""Verify WindTurbineBlade.py's geometry against the BOM, outside Fusion.

The Fusion API half of that script cannot be executed here. The GEOMETRY half
can, because the adsk import is guarded -- so every number the loft is built
from is checkable without launching Fusion. That is the point of the split.

Checks performed:
  1. Airfoil thickness matches the requested t/c (the generator is correct)
  2. Contours do not self-intersect (upper surface stays above lower)
  3. Trailing edge is blunt, not cusped (a cusp lofts badly in Fusion)
  4. Circle and airfoil point counts match, so the morph pairs correct points
  5. Planform hits the BOM's stated root / max / tip chord values
  6. Twist, thickness and prebend hit their BOM endpoints
  7. Chord and thickness vary smoothly (no crease in the lofted surface)
  8. Spar caps and shear webs stay inside the aeroshell
  9. No NaN or Inf anywhere in any station
"""

import math
import sys

from WindTurbineBlade import (
    CONFIG, airfoil_section, circle_section, blended_section,
    chord_at, twist_at, thickness_at, prebend_at,
    station_points, station_points_local, span_stations,
    spar_cap_rect, shear_web_rect,
    spar_cap_rect_local, shear_web_rect_local,
    surface_y_at, naca4_thickness,
)

cfg = CONFIG
n = cfg['n_airfoil_points']
fails = []
notes = []


def check(label, ok, detail=''):
    print('  [%s] %s%s' % ('PASS' if ok else 'FAIL', label,
                           ('  -- ' + detail) if detail else ''))
    if not ok:
        fails.append(label)


def surfaces(pts):
    """Split a contour back into paired upper/lower points at equal x-stations."""
    m = (len(pts) + 1) // 2
    upper = [pts[m - 1 - k] for k in range(m)]
    lower = [pts[m - 1 + k] for k in range(m)]
    return upper, lower


print('=' * 70)
print('1. Airfoil generator: measured t/c vs requested')
print('=' * 70)
for tc in (0.18, 0.25, 0.30, 0.40):
    pts = airfoil_section(tc, n, cfg['camber_frac'], cfg['camber_pos'])
    up, lo = surfaces(pts)
    measured = max(math.hypot(u[0] - l[0], u[1] - l[1]) for u, l in zip(up, lo))
    err = abs(measured - tc)
    check('t/c = %.2f  ->  measured %.4f' % (tc, measured), err < 0.005,
          'error %.5f' % err)

print()
print('=' * 70)
print('2. Contour integrity')
print('=' * 70)
for tc in (0.18, 0.40):
    pts = airfoil_section(tc, n, cfg['camber_frac'], cfg['camber_pos'])
    up, lo = surfaces(pts)
    # skip the shared LE point at k=0
    crossings = sum(1 for k in range(1, len(up)) if up[k][1] <= lo[k][1])
    check('t/c %.2f: upper stays above lower' % tc, crossings == 0,
          '%d crossing(s)' % crossings)

    te_gap = math.hypot(pts[0][0] - pts[-1][0], pts[0][1] - pts[-1][1])
    check('t/c %.2f: trailing edge blunt, not cusped' % tc,
          1e-4 < te_gap < 0.05, 'TE gap %.5f c' % te_gap)

check('circle and airfoil have equal point counts',
      len(circle_section(n)) == len(airfoil_section(0.4, n, 0.04, 0.4)),
      '%d vs %d' % (len(circle_section(n)),
                    len(airfoil_section(0.4, n, 0.04, 0.4))))

circ = circle_section(n)
up, lo = surfaces(circ)
dia = max(math.hypot(u[0] - l[0], u[1] - l[1]) for u, l in zip(up, lo))
check('root circle is circular, diameter 1.0 normalised', abs(dia - 1.0) < 1e-6,
      'measured %.6f' % dia)

print()
print('=' * 70)
print('3. Planform vs BOM 1100')
print('=' * 70)
check('root chord = root diameter %.2f m' % cfg['root_diameter_m'],
      abs(chord_at(0.0, cfg) - cfg['root_diameter_m']) < 1e-6,
      '%.4f m' % chord_at(0.0, cfg))
check('max chord = %.2f m at r/R = %.2f' % (cfg['max_chord_m'],
                                            cfg['max_chord_station']),
      abs(chord_at(cfg['max_chord_station'], cfg) - cfg['max_chord_m']) < 1e-6,
      '%.4f m' % chord_at(cfg['max_chord_station'], cfg))
check('tip chord = %.2f m' % cfg['tip_chord_m'],
      abs(chord_at(1.0, cfg) - cfg['tip_chord_m']) < 1e-6,
      '%.4f m' % chord_at(1.0, cfg))

# the peak really is the maximum, not just a matched value
samples = [chord_at(i / 400.0, cfg) for i in range(401)]
peak = max(samples)
check('max chord is the global planform maximum',
      abs(peak - cfg['max_chord_m']) < 1e-3, 'global max %.4f m' % peak)

print()
print('=' * 70)
print('4. Twist, thickness, prebend endpoints')
print('=' * 70)
check('twist at root = %.1f deg' % cfg['twist_total_deg'],
      abs(twist_at(0.0, cfg) - cfg['twist_total_deg']) < 1e-9,
      '%.3f deg' % twist_at(0.0, cfg))
check('twist at tip = 0 deg', abs(twist_at(1.0, cfg)) < 1e-9,
      '%.3f deg' % twist_at(1.0, cfg))
check('twist decreases monotonically',
      all(twist_at(i / 200.0, cfg) >= twist_at((i + 1) / 200.0, cfg) - 1e-12
          for i in range(200)))

check('t/c at root = %.2f (circular)' % cfg['tc_root'],
      abs(thickness_at(0.0, cfg) - cfg['tc_root']) < 1e-9,
      '%.4f' % thickness_at(0.0, cfg))
check('t/c at tip = %.2f' % cfg['tc_tip'],
      abs(thickness_at(1.0, cfg) - cfg['tc_tip']) < 1e-9,
      '%.4f' % thickness_at(1.0, cfg))
check('t/c decreases monotonically',
      all(thickness_at(i / 200.0, cfg) >= thickness_at((i + 1) / 200.0, cfg) - 1e-12
          for i in range(200)))

check('prebend at root = 0', abs(prebend_at(0.0, cfg)) < 1e-12)
check('prebend at tip = %.1f m' % cfg['prebend_tip_m'],
      abs(prebend_at(1.0, cfg) - cfg['prebend_tip_m']) < 1e-9,
      '%.4f m' % prebend_at(1.0, cfg))
# cubic => zero slope at the root, which is what a cantilever looks like
slope_root = (prebend_at(1e-4, cfg) - prebend_at(0.0, cfg)) / 1e-4
check('prebend has ~zero slope at the root', abs(slope_root) < 1e-6,
      'slope %.3e' % slope_root)

print()
print('=' * 70)
print('5. No derivative singularities')
print('=' * 70)
# The criterion here is BOUNDED SLOPE, not bounded curvature.
#
# A blade planform legitimately has a max-chord shoulder: a finite jump in
# slope where the inboard rise meets the outboard taper. Demanding bounded
# curvature would flag that real feature. What is NOT legitimate is an
# unbounded slope, which is what an exponent < 1 on the outboard taper
# produces -- and that pinches the loft. So: bound the first derivative,
# and separately confirm the shoulder jump is finite.
N = 2000
def slope(f, i):
    return (f((i + 1) / N, cfg) - f(i / N, cfg)) * N

cs = [abs(slope(chord_at, i)) for i in range(N)]
check('chord slope bounded (no singularity)', max(cs) < 20.0,
      'max |d chord/d(r/R)| = %.3f m' % max(cs))

ts = [abs(slope(thickness_at, i)) for i in range(N)]
check('t/c slope bounded (no singularity)', max(ts) < 20.0,
      'max |d(t/c)/d(r/R)| = %.3f' % max(ts))

ps = [abs(slope(prebend_at, i)) for i in range(N)]
check('prebend slope bounded', max(ps) < 20.0,
      'max |d prebend/d(r/R)| = %.3f m' % max(ps))

# The shoulder itself: finite jump, and only one of them.
jumps = [abs(cs[i + 1] - cs[i]) for i in range(N - 1)]
big = [i for i, j in enumerate(jumps) if j > 1.0]
check('at most one slope discontinuity (the max-chord shoulder)',
      len(big) <= 1,
      'jump(s) at r/R = %s' % [round(i / N, 3) for i in big])

print()
print('=' * 70)
print('6. Station geometry: finiteness and structure containment')
print('=' * 70)
bad_vals = 0
for rR in span_stations(cfg):
    for (x, y) in station_points(rR, cfg):
        if not (math.isfinite(x) and math.isfinite(y)):
            bad_vals += 1
check('all station points finite (no NaN/Inf)', bad_vals == 0,
      '%d bad value(s)' % bad_vals)

# Spar caps and webs must lie inside the SKIN, not merely inside its bounding
# box. A bounding-box test passes a cap that has punched through a cambered
# lower surface, because the box still contains it -- which is precisely the
# bug this check exists to catch. Use point-in-polygon against the real
# contour, in the untwisted frame where both are defined.
def inside(poly, pt):
    """Ray-casting point-in-polygon."""
    x, y = pt
    n = len(poly)
    hit = False
    for i in range(n):
        x0, y0 = poly[i]
        x1, y1 = poly[(i + 1) % n]
        if (y0 > y) != (y1 > y):
            xc = x0 + (y - y0) * (x1 - x0) / (y1 - y0)
            if x < xc:
                hit = not hit
    return hit


outside = []
for rR in span_stations(cfg):
    if not (cfg['structure_root_frac'] <= rR <= cfg['structure_tip_frac']):
        continue
    shell = station_points_local(rR, cfg)
    for label, pts in (('SparCapUpper', spar_cap_rect_local(rR, cfg, True)),
                       ('SparCapLower', spar_cap_rect_local(rR, cfg, False)),
                       ('WebFore',      shear_web_rect_local(rR, cfg, True)),
                       ('WebAft',       shear_web_rect_local(rR, cfg, False))):
        for p in pts:
            if not inside(shell, p):
                outside.append((label, round(rR, 3)))
                break

check('spar caps and webs strictly inside the skin (point-in-polygon)',
      len(outside) == 0,
      '%d violation(s): %s' % (len(outside), sorted(set(outside))[:6]))

# The transform must be shared: structure and skin have to move together.
rR_t = 0.5
skin_t = station_points(rR_t, cfg)
skin_l = station_points_local(rR_t, cfg)
cap_t = spar_cap_rect(rR_t, cfg, True)
cap_l = spar_cap_rect_local(rR_t, cfg, True)
moved_skin = max(math.hypot(a[0] - b[0], a[1] - b[1])
                 for a, b in zip(skin_t, skin_l))
moved_cap = max(math.hypot(a[0] - b[0], a[1] - b[1])
                for a, b in zip(cap_t, cap_l))
check('skin and spar cap share one twist/prebend transform',
      moved_skin > 1e-6 and moved_cap > 1e-6 and
      all(inside(station_points(rR_t, cfg), p)
          for p in spar_cap_rect(rR_t, cfg, True)),
      'cap still inside skin after transform')

print()
print('=' * 70)
print('7. Planform table (for eyeballing against the BOM)')
print('=' * 70)
print('   r/R    chord[m]  twist[deg]   t/c    prebend[m]   z[m]')
for rR in (0.0, 0.05, 0.10, 0.22, 0.35, 0.50, 0.75, 0.90, 1.0):
    z = cfg['hub_radius_m'] + rR * cfg['blade_length_m']
    print('  %.2f    %6.3f     %5.2f     %5.3f     %5.3f     %6.2f'
          % (rR, chord_at(rR, cfg), twist_at(rR, cfg),
             thickness_at(rR, cfg), prebend_at(rR, cfg), z))

print()
print('=' * 70)
if fails:
    print('FAILED %d check(s):' % len(fails))
    for f in fails:
        print('  -', f)
    sys.exit(1)
else:
    print('ALL GEOMETRY CHECKS PASSED')
    print()
    print('Verified: the airfoil generator, the planform/twist/thickness/prebend')
    print('distributions, contour integrity, and structure containment.')
    print('NOT verified: any Fusion API call. Those need Fusion to run.')
