struct CrtUniforms {
    resolution: vec2<f32>,
    time: f32,
    _pad: f32,
}

@group(0) @binding(0)
var<uniform> u: CrtUniforms;

struct VsOut {
    @builtin(position) pos: vec4<f32>,
    @location(0) uv: vec2<f32>,
}

@vertex
fn vs_main(@builtin(vertex_index) i: u32) -> VsOut {
    var p = array<vec2<f32>, 3>(
        vec2<f32>(-1.0, -1.0),
        vec2<f32>(3.0, -1.0),
        vec2<f32>(-1.0, 3.0),
    );
    var out: VsOut;
    out.pos = vec4<f32>(p[i], 0.0, 1.0);
    out.uv = p[i] * 0.5 + vec2<f32>(0.5, 0.5);
    return out;
}

fn hash21(p: vec2<f32>) -> f32 {
    var q = fract(p * vec2<f32>(123.34, 456.21));
    q = q + dot(q, q + 34.23);
    return fract(q.x * q.y);
}

@fragment
fn fs_main(in: VsOut) -> @location(0) vec4<f32> {
    var uv = in.uv;
    let centered = uv * 2.0 - 1.0;
    let barrel = 0.06 * dot(centered, centered);
    uv = uv + centered * barrel * 0.15;

    let guide = vec3<f32>(0.02, 0.07, 0.06);
    let phosphor = vec3<f32>(0.12, 0.55, 0.38);
    let amber = vec3<f32>(0.85, 0.55, 0.18);
    let grid_y = floor(uv.y * 18.0);
    let band = mix(guide, guide * 1.35, (sin(grid_y * 1.7) * 0.5 + 0.5) * 0.35);

    let scan = 0.82 + 0.18 * sin(uv.y * u.resolution.y * 3.14159);
    let flicker = 0.985 + 0.015 * sin(u.time * 47.0);
    let grain = (hash21(uv * u.resolution + u.time) - 0.5) * 0.04;

    let vig = smoothstep(1.25, 0.25, length(centered * vec2<f32>(1.05, 1.15)));
    let roll = 0.012 * sin(uv.y * 40.0 + u.time * 1.7);

    var col = band * scan * flicker;
    col = col + phosphor * (0.08 + 0.05 * sin(u.time * 0.7 + uv.x * 6.0));
    col = col + amber * 0.03 * (1.0 - uv.y);
    col.g = col.g + roll * 0.15;
    col = col + vec3<f32>(grain);
    col = col * vig;

    return vec4<f32>(col, 1.0);
}
