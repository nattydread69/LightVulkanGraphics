#version 450

layout(set = 1, binding = 0) uniform sampler3D volumeTexture;
layout(set = 1, binding = 1) uniform sampler2D transferFunction;

// Spare components carry the render style (lightVulkanGraphics 3.2) so the block keeps
// its 128-byte size:
//   flags.x  bit 0 jitter, 1 early termination, 2 clip plane, 3 clip box,
//            4 extinction opacity, 5 step normalisation, 6 RGBA texture colour,
//            7 gradient shading
//   flags.y  style: 0 composite, 1 maximum intensity, 2 minimum intensity,
//            3 isosurface, 4 average (radiograph)
//   volumeMinimum.w  isovalue (normalised scalar) for the isosurface style
//   volumeMaximum.w  shading strength 0..1 (0 = flat)
layout(push_constant) uniform VolumePushConstants
{
	vec4 cameraWorld;
	vec4 volumeMinimum;
	vec4 volumeMaximum;
	vec4 clipPlane;
	vec4 clipBoxMinimum;
	vec4 clipBoxMaximum;
	vec4 settings;
	ivec4 flags;
} volume;

layout(location = 0) in vec3 texturePosition;
layout(location = 0) out vec4 outColor;

const int STYLE_COMPOSITE = 0;
const int STYLE_MAXIMUM = 1;
const int STYLE_MINIMUM = 2;
const int STYLE_ISOSURFACE = 3;
const int STYLE_AVERAGE = 4;

float randomValue(vec2 coordinate)
{
	return fract(sin(dot(coordinate, vec2(12.9898, 78.233))) * 43758.5453);
}

bool flagEnabled(int bitIndex)
{
	return (volume.flags.x & (1 << bitIndex)) != 0;
}

bool textureColor()
{
	return flagEnabled(6);
}

// The normalised scalar at a texture position (alpha in RGBA mode).
float scalarAt(vec3 p)
{
	vec4 texel = texture(volumeTexture, p);
	return clamp((textureColor() ? texel.a : texel.r) * volume.settings.x, 0.0, 1.0);
}

bool insideVolume(vec3 samplePosition)
{
	vec3 worldPosition = mix(volume.volumeMinimum.xyz, volume.volumeMaximum.xyz, samplePosition);
	bool clipped = flagEnabled(2) && dot(worldPosition, volume.clipPlane.xyz) > volume.clipPlane.w;
	clipped = clipped || (flagEnabled(3) &&
		(any(lessThan(samplePosition, volume.clipBoxMinimum.xyz)) ||
		 any(greaterThan(samplePosition, volume.clipBoxMaximum.xyz))));
	return !clipped && all(greaterThanEqual(samplePosition, vec3(0.0))) &&
		all(lessThanEqual(samplePosition, vec3(1.0)));
}

// World-space gradient of the scalar (central differences, one texel apart).
vec3 gradientAt(vec3 p, vec3 extent)
{
	vec3 texel = 1.0 / vec3(textureSize(volumeTexture, 0));
	vec3 g = vec3(
		scalarAt(p + vec3(texel.x, 0.0, 0.0)) - scalarAt(p - vec3(texel.x, 0.0, 0.0)),
		scalarAt(p + vec3(0.0, texel.y, 0.0)) - scalarAt(p - vec3(0.0, texel.y, 0.0)),
		scalarAt(p + vec3(0.0, 0.0, texel.z)) - scalarAt(p - vec3(0.0, 0.0, texel.z)));
	return g / (2.0 * texel * extent);
}

// Headlight Blinn-Phong, two-sided (the gradient's sign depends on the data).
vec3 shade(vec3 rgb, vec3 gradient, vec3 viewDirection, float strength)
{
	float magnitude = length(gradient);
	if (magnitude < 1.0e-6 || strength <= 0.0)
	{
		return rgb;
	}
	vec3 n = gradient / magnitude;
	vec3 l = -viewDirection;
	float diffuse = abs(dot(n, l));
	float specular = pow(diffuse, 40.0) * 0.35;
	vec3 lit = rgb * (0.25 + 0.75 * diffuse) + vec3(specular);
	return mix(rgb, lit, strength);
}

vec4 colourOf(vec3 p, float scalar)
{
	vec4 c = texture(transferFunction, vec2(scalar, 0.5));
	if (textureColor())
	{
		c.rgb = texture(volumeTexture, p).rgb;
	}
	return c;
}

void main()
{
	vec3 extent = volume.volumeMaximum.xyz - volume.volumeMinimum.xyz;
	vec3 cameraTexture = (volume.cameraWorld.xyz - volume.volumeMinimum.xyz) / extent;
	vec3 direction = normalize(texturePosition - cameraTexture);
	vec3 worldDirection = normalize(direction * extent);
	vec3 directionSign = mix(
		vec3(-1.0), vec3(1.0), greaterThanEqual(direction, vec3(0.0)));
	vec3 safeDirection = directionSign * max(abs(direction), vec3(1.0e-6));
	vec3 toMinimum = (vec3(0.0) - texturePosition) / safeDirection;
	vec3 toMaximum = (vec3(1.0) - texturePosition) / safeDirection;
	vec3 exitDistance = max(toMinimum, toMaximum);
	float maximumDistance = min(exitDistance.x, min(exitDistance.y, exitDistance.z));
	int stepCount = clamp(int(volume.settings.z + 0.5), 2, 2048);
	float stepLength = maximumDistance / float(stepCount);
	float jitter = flagEnabled(0) ? randomValue(gl_FragCoord.xy) : 0.5;
	vec3 samplePosition = texturePosition + direction * stepLength * jitter;
	int style = volume.flags.y;
	float shading = flagEnabled(7) ? clamp(volume.volumeMaximum.w, 0.0, 1.0) : 0.0;

	if (style == STYLE_MAXIMUM || style == STYLE_MINIMUM || style == STYLE_AVERAGE)
	{
		// Projections: one value per ray, then the transfer function.
		float best = style == STYLE_MINIMUM ? 2.0 : -1.0;
		vec3 bestPosition = samplePosition;
		float sum = 0.0;
		int count = 0;
		for (int index = 0; index < 2048; ++index)
		{
			if (index >= stepCount)
			{
				break;
			}
			if (insideVolume(samplePosition))
			{
				float s = scalarAt(samplePosition);
				sum += s;
				count += 1;
				if ((style == STYLE_MAXIMUM && s > best) || (style == STYLE_MINIMUM && s < best))
				{
					best = s;
					bestPosition = samplePosition;
				}
			}
			samplePosition += direction * stepLength;
		}
		if (count == 0)
		{
			outColor = vec4(0.0);
			return;
		}
		float value = style == STYLE_AVERAGE ? sum / float(count) : best;
		vec4 c = colourOf(bestPosition, value);
		outColor = vec4(c.rgb, clamp(c.a * max(volume.settings.y * 4.0, 0.0), 0.0, 1.0));
		return;
	}

	if (style == STYLE_ISOSURFACE)
	{
		// First crossing of the isovalue, refined by bisection, shaded by the gradient.
		float iso = clamp(volume.volumeMinimum.w, 0.0, 1.0);
		vec3 previous = samplePosition;
		bool previousInside = false;
		for (int index = 0; index < 2048; ++index)
		{
			if (index >= stepCount)
			{
				break;
			}
			bool inside = insideVolume(samplePosition);
			if (inside && scalarAt(samplePosition) >= iso)
			{
				vec3 lo = previousInside ? previous : samplePosition;
				vec3 hi = samplePosition;
				for (int k = 0; k < 6 && previousInside; ++k)
				{
					vec3 mid = 0.5 * (lo + hi);
					if (scalarAt(mid) >= iso)
					{
						hi = mid;
					}
					else
					{
						lo = mid;
					}
				}
				vec4 c = colourOf(hi, max(scalarAt(hi), iso));
				vec3 rgb = shade(c.rgb, gradientAt(hi, extent), worldDirection, max(shading, 0.85));
				outColor = vec4(rgb, 1.0);
				return;
			}
			previous = samplePosition;
			previousInside = inside;
			samplePosition += direction * stepLength;
		}
		outColor = vec4(0.0);
		return;
	}

	vec4 accumulated = vec4(0.0);
	for (int index = 0; index < 2048; ++index)
	{
		if (index >= stepCount)
		{
			break;
		}
		if (insideVolume(samplePosition))
		{
			// Flag 6 (VolumeColorSource::TextureRgba): colour from the texture's
			// RGB, and its alpha is the scalar the transfer function maps to opacity
			float scalar = scalarAt(samplePosition);
			vec4 sampleColor = colourOf(samplePosition, scalar);
			if (flagEnabled(4))
			{
				float physicalStepLength = length(direction * extent * stepLength);
				float normalizedStepLength = flagEnabled(5)
					? physicalStepLength / max(volume.settings.w, 1.0e-6)
					: 1.0;
				float extinction = max(volume.settings.x * sampleColor.a *
					volume.settings.y * normalizedStepLength, 0.0);
				sampleColor.a = clamp(1.0 - exp(-extinction), 0.0, 1.0);
			}
			else
			{
				sampleColor.a = clamp(sampleColor.a * volume.settings.y, 0.0, 1.0);
			}
			if (shading > 0.0 && sampleColor.a > 0.002)
			{
				sampleColor.rgb = shade(sampleColor.rgb, gradientAt(samplePosition, extent), worldDirection, shading);
			}
			sampleColor.rgb *= sampleColor.a;
			accumulated += (1.0 - accumulated.a) * sampleColor;
			if (flagEnabled(1) && accumulated.a >= 0.985)
			{
				break;
			}
		}
		samplePosition += direction * stepLength;
	}
	outColor = accumulated.a > 0.0
		? vec4(accumulated.rgb / accumulated.a, accumulated.a)
		: vec4(0.0);
}
