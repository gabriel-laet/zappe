struct QuadUniforms {
    resolution: vec2<f32>,
    _pad: vec2<f32>,
}

struct Instance {
    rect: vec4<f32>,
    color: vec4<f32>,
    extra: vec4<f32>,
}

@group(0) @binding(0)
var<uniform> u: QuadUniforms;
@group(0) @binding(1)
var font_tex: texture_2d<f32>;
@group(0) @binding(2)
var font_samp: sampler;

struct VsIn {
    @location(0) pos: vec2<f32>,
    @location(1) uv: vec2<f32>,
    @location(2) rect: vec4<f32>,
    @location(3) color: vec4<f32>,
    @location(4) extra: vec4<f32>,
}

struct VsOut {
    @builtin(position) pos: vec4<f32>,
    @location(0) uv: vec2<f32>,
    @location(1) color: vec4<f32>,
    @location(2) extra: vec4<f32>,
    @location(3) local: vec2<f32>,
    @location(4) size: vec2<f32>,
}

@vertex
fn vs_main(v: VsIn) -> VsOut {
    let px = v.rect.xy + v.rect.zw * v.pos;
    let ndc = (px / u.resolution) * 2.0 - 1.0;
    var out: VsOut;
    out.pos = vec4<f32>(ndc.x, -ndc.y, 0.0, 1.0);
    out.uv = v.uv;
    out.color = v.color;
    out.extra = v.extra;
    out.local = v.pos;
    out.size = v.rect.zw;
    return out;
}

fn rounded_box(p: vec2<f32>, b: vec2<f32>, r: f32) -> f32 {
    let q = abs(p) - b + vec2<f32>(r, r);
    return length(max(q, vec2<f32>(0.0))) + min(max(q.x, q.y), 0.0) - r;
}

@fragment
fn fs_main(in: VsOut) -> @location(0) vec4<f32> {
    let kind = in.extra.x;
    if kind > 1.5 {
        // TV focus ring: fat rounded stroke, not a 1px outline.
        let half = in.size * 0.5;
        let p = (in.local - vec2<f32>(0.5, 0.5)) * in.size;
        let radius = max(in.extra.y, 6.0);
        let d = rounded_box(p, half, radius);
        let thick = max(in.extra.z, 10.0);
        let aa = 1.8;
        let ring = 1.0 - smoothstep(0.0, aa, abs(d) - thick * 0.5);
        let halo = 0.35 * (1.0 - smoothstep(thick * 0.5, thick * 2.4, abs(d)));
        let alpha = max(ring, halo);
        if alpha < 0.02 {
            discard;
        }
        return vec4<f32>(in.color.rgb, in.color.a * alpha);
    }
    if kind > 0.5 {
        let atlas = vec2<f32>(16.0, 6.0);
        let glyph = in.extra.yz;
        let uv = (glyph + in.uv) / atlas;
        let a = textureSample(font_tex, font_samp, uv).r;
        if a < 0.5 {
            discard;
        }
        return vec4<f32>(in.color.rgb, in.color.a);
    }

    let half = in.size * 0.5;
    let p = (in.local - vec2<f32>(0.5, 0.5)) * in.size;
    let radius = max(in.extra.y, 2.0);
    let d = rounded_box(p, half, radius);
    let aa = 1.2;
    var alpha = 1.0 - smoothstep(0.0, aa, d);
    if alpha < 0.01 {
        discard;
    }

    let focused = in.extra.z;
    let glow = smoothstep(10.0, -2.0, d) * focused * 0.45;
    var col = in.color.rgb + vec3<f32>(0.25, 0.55, 0.35) * glow;
    let scan = 0.92 + 0.08 * sin((in.local.y * in.size.y) * 0.7);
    col = col * scan;
    return vec4<f32>(col, in.color.a * alpha);
}
