/// @file LightVulkanGraphicsPublic.h
/// @brief Public API facade for LightVulkanGraphics library
/// @version 2.1.0
/// @date 2026
///
/// This header provides the main entry point for using LightVulkanGraphics.
/// Include this header to access all public components of the library.
///
/// @example
/// @code
/// #include "LightVulkanGraphicsPublic.h"
/// using namespace lightGraphics;
///
/// // Create application
/// lightVulkanGraphics app("My Application");
/// app.run();
/// @endcode

#pragma once

// Version information
#include "LightVulkanGraphicsVersion.h"

// ============================================================================
// CORE COMPONENT - Essential graphics functionality
// ============================================================================
/// @defgroup Core Core Graphics API
/// @brief Core graphics rendering, object management, and scene graph

/// Main application/graphics engine
#include "lightVulkanGraphics.h"

/// Scene graph and object management
#include "SceneGraph.h"
#include "pObject.h"
#include "GraphicsModel.h"

/// Camera and view management
#include "Camera.h"

/// Lighting
#include "Light.h"

// ============================================================================
// LOADERS COMPONENT - Model and asset loading
// ============================================================================
/// @defgroup Loaders Asset Loaders
/// @brief Loading models and rigged assets from files

/// FBX file loader
#include "FBXLoader.h"

/// Rigged model support
#include "RiggedObject.h"

// ============================================================================
// ============================================================================
// UI COMPONENT - LVGUI graphical user interface
// ============================================================================
/// @defgroup UI GUI Framework
/// @brief Immediate-mode GUI library (LVGUI)
#ifdef LVG_BUILD_UI
	#include "lightVulkanGraphics/ui/GuiContext.h"
	#include "lightVulkanGraphics/ui/Panel.h"
	#include "lightVulkanGraphics/ui/Widget.h"

	// Common widgets
	#include "lightVulkanGraphics/ui/widgets/Button.h"
	#include "lightVulkanGraphics/ui/widgets/Label.h"
	#include "lightVulkanGraphics/ui/widgets/Slider.h"
	#include "lightVulkanGraphics/ui/widgets/DropDown.h"
	#include "lightVulkanGraphics/ui/widgets/RadioButton.h"
	#include "lightVulkanGraphics/ui/widgets/Checkbox.h"
	#include "lightVulkanGraphics/ui/widgets/TextBox.h"
#endif

// ============================================================================
// ADVANCED COMPONENT - Specialized rendering techniques
// ============================================================================
/// @defgroup Advanced Advanced Rendering
/// @brief Advanced rendering features and volume visualization

/// Volume rendering support
#include "VolumeRendering.h"

/// Rotation glyph visualization
#include "RotationGlyph.h"

// ============================================================================
// UTILITIES
// ============================================================================

/// Logging utilities
#include "LightVulkanGraphicsLogging.h"

/// Common header declarations
#include "pHeaders.h"

/// @namespace lightGraphics
/// @brief Main namespace for LightVulkanGraphics library

#endif // LIGHT_VULKAN_GRAPHICS_PUBLIC_H
