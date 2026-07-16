#version 450

layout(set = 1, binding = 0) uniform sampler3D volumeTexture;
layout(set = 1, binding = 1) uniform sampler2D transferFunction;

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

float randomValue(vec2 coordinate)
{
	return fract(sin(dot(coordinate, vec2(12.9898, 78.233))) * 43758.5453);
}

bool flagEnabled(int bitIndex)
{
	return (volume.flags.x & (1 << bitIndex)) != 0;
}

void main()
{
	vec3 extent = volume.volumeMaximum.xyz - volume.volumeMinimum.xyz;
	vec3 cameraTexture = (volume.cameraWorld.xyz - volume.volumeMinimum.xyz) / extent;
	vec3 direction = normalize(texturePosition - cameraTexture);
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
	vec4 accumulated = vec4(0.0);

	for (int index = 0; index < 2048; ++index)
	{
		if (index >= stepCount)
		{
			break;
		}
		vec3 worldPosition = mix(
			volume.volumeMinimum.xyz,
			volume.volumeMaximum.xyz,
			samplePosition);
		bool clipped = flagEnabled(2) &&
			dot(worldPosition, volume.clipPlane.xyz) > volume.clipPlane.w;
		clipped = clipped || (flagEnabled(3) &&
			(any(lessThan(samplePosition, volume.clipBoxMinimum.xyz)) ||
			 any(greaterThan(samplePosition, volume.clipBoxMaximum.xyz))));
		if (!clipped && all(greaterThanEqual(samplePosition, vec3(0.0))) &&
			all(lessThanEqual(samplePosition, vec3(1.0))))
		{
			float scalar = clamp(texture(volumeTexture, samplePosition).r *
				volume.settings.x, 0.0, 1.0);
			vec4 sampleColor = texture(transferFunction, vec2(scalar, 0.5));
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
