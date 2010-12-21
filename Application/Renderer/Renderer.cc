/*
 For more information, please see: http://software.sci.utah.edu

 The MIT License

 Copyright (c) 2009 Scientific Computing and Imaging Institute,
 University of Utah.


 Permission is hereby granted, free of charge, to any person obtaining a
 copy of this software and associated documentation files (the "Software"),
 to deal in the Software without restriction, including without limitation
 the rights to use, copy, modify, merge, publish, distribute, sublicense,
 and/or sell copies of the Software, and to permit persons to whom the
 Software is furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included
 in all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 DEALINGS IN THE SOFTWARE.
 */

// Boost includes
#include <boost/lexical_cast.hpp>

// Core includes
#include <Core/Application/Application.h>
#include <Core/Utils/Log.h>
#include <Core/RenderResources/RenderResources.h>
#include <Core/Geometry/View3D.h>
#include <Core/Graphics/UnitCube.h>
#include <Core/Geometry/BBox.h>
#include <Core/Graphics/Texture.h>
#include <Core/Graphics/UnitCube.h>
#include <Core/TextRenderer/TextRenderer.h>
#include <Core/Graphics/ColorMap.h>

// Application includes
#include <Application/Layer/DataLayer.h>
#include <Application/Layer/MaskLayer.h>
#include <Application/Layer/LayerGroup.h>
#include <Application/LayerManager/LayerManager.h>
#include <Application/LayerManager/LayerScene.h>
#include <Application/PreferencesManager/PreferencesManager.h>
#include <Application/Renderer/Renderer.h>
#include <Application/Tool/Tool.h>
#include <Application/ToolManager/ToolManager.h>
#include <Application/ViewerManager/ViewerManager.h>
#include <Application/Renderer/SliceShader.h>
#include <Application/Renderer/IsosurfaceShader.h>
#include <Application/Viewer/Viewer.h>
#include <Application/Renderer/OrientationArrows.h>


namespace Seg3D
{

//////////////////////////////////////////////////////////////////////////
// Implementation of class ProxyRectangle
//////////////////////////////////////////////////////////////////////////

class ProxyRectangle;
typedef boost::shared_ptr< ProxyRectangle > ProxyRectangleHandle;

class ProxyRectangle
{
public:
	Core::Point bottomleft, bottomright, topleft, topright;
	double left, right, bottom, top;
	Core::Vector normal;
};

//////////////////////////////////////////////////////////////////////////
// Implementation of class IsosurfaceRecord
//////////////////////////////////////////////////////////////////////////
class IsosurfaceRecord
{
public:
	Core::Color color_;
	Core::IsosurfaceHandle isosurface_;
};
typedef boost::shared_ptr< IsosurfaceRecord > IsosurfaceRecordHandle;
typedef std::vector< IsosurfaceRecordHandle > IsosurfaceArray;

//////////////////////////////////////////////////////////////////////////
// Implementation of class RendererPrivate
//////////////////////////////////////////////////////////////////////////

static const unsigned int PATTERN_SIZE_C = 6;
static const unsigned char MAX_PATTERN_VAL_C = 150;
static const unsigned char MASK_PATTERNS_C[ PATTERN_SIZE_C ][ PATTERN_SIZE_C ] =
{
	{ MAX_PATTERN_VAL_C, 0, 0, MAX_PATTERN_VAL_C, 0, 0 }, 
	{ 0, MAX_PATTERN_VAL_C, 0, 0, MAX_PATTERN_VAL_C, 0 }, 
	{ 0, 0, MAX_PATTERN_VAL_C, 0, 0, MAX_PATTERN_VAL_C }, 
	{ MAX_PATTERN_VAL_C, 0, 0, MAX_PATTERN_VAL_C, 0, 0 },
	{ 0, MAX_PATTERN_VAL_C, 0, 0, MAX_PATTERN_VAL_C, 0 }, 
	{ 0, 0, MAX_PATTERN_VAL_C, 0, 0, MAX_PATTERN_VAL_C }
};

class RendererPrivate
{
	// -- Helper functions --
public:
	void process_slices( LayerSceneHandle& layer_scene, ViewerHandle& viewer );
	void draw_slices_3d( const Core::BBox& bbox, 
		const std::vector< LayerSceneHandle >& layer_scenes, 
		const std::vector< double >& depths,
		const std::vector< std::string >& view_modes,
		bool with_lighting );
	void draw_slice( LayerSceneItemHandle layer_item, const Core::Matrix& proj_mat,
		ProxyRectangleHandle rect = ProxyRectangleHandle() );
	void process_isosurfaces( IsosurfaceArray& isosurfaces );
	void draw_isosurfaces( const IsosurfaceArray& isosurfaces, bool with_lighting );
	void draw_orientation_arrows( const Core::View3D& view_3d );

	// -- Signals handling --
public:
	void viewer_slice_changed( size_t viewer_id );
	void viewer_mode_changed( size_t viewer_id );
	void picking_target_changed( size_t viewer_id );
	void enable_rendering( bool enable );

	Renderer* renderer_;

	OrientationArrowsHandle orientation_arrows_;
	SliceShaderHandle slice_shader_;
	IsosurfaceShaderHandle isosurface_shader_;
	Core::Texture2DHandle pattern_texture_;
	Core::TextRendererHandle text_renderer_;
	Core::Texture2DHandle text_texture_;

	size_t viewer_id_;
	bool rendering_enabled_;
};

void RendererPrivate::process_slices( LayerSceneHandle& layer_scene, ViewerHandle& viewer )
{
	for ( size_t layer_num = 0; layer_num < layer_scene->size(); layer_num++ )
	{
		LayerSceneItemHandle layer_item = ( *layer_scene )[ layer_num ];
		bool layer_visible = false;
		if ( layer_item->layer_->is_visible( this->viewer_id_ ) )
		{
			Core::VolumeSliceHandle volume_slice = 
				viewer->get_volume_slice( layer_item->layer_id_ );

			if ( volume_slice && !volume_slice->out_of_boundary() )
			{
				volume_slice->initialize_texture();
				volume_slice->upload_texture();
				layer_item->volume_slice_ = volume_slice->clone();
				layer_visible = true;
			}
		}
		
		// Release the handle to the layer
		layer_item->layer_.reset();

		// Remove the LayerSceneItem if it's not visible in the current viewer
		if ( !layer_visible )
		{
			layer_scene->erase( layer_scene->begin() + layer_num );
			layer_num--;
		}

	} // end for

	// Make the bottom layer fully opaque
	if ( layer_scene->size() != 0 )
	{
		( *layer_scene )[ 0 ]->opacity_ = 1.0;
	}
}

void RendererPrivate::viewer_slice_changed( size_t viewer_id )
{
	ViewerHandle self_viewer = ViewerManager::Instance()->get_viewer( this->viewer_id_ );
	ViewerHandle updated_viewer = ViewerManager::Instance()->get_viewer( viewer_id );
	if ( self_viewer->view_mode_state_->index() !=
		updated_viewer->view_mode_state_->index() )
	{
		if ( self_viewer->is_volume_view() )
		{
			this->renderer_->redraw_scene();
		}
		else
		{
			this->renderer_->redraw_overlay();
		}
	}
}

void RendererPrivate::viewer_mode_changed( size_t viewer_id )
{
	if ( ViewerManager::Instance()->get_viewer( viewer_id )->viewer_visible_state_->get() )
	{
		if ( ViewerManager::Instance()->get_viewer( this->viewer_id_ )->is_volume_view() )
		{
			this->renderer_->redraw_scene();
		}
		else
		{
			this->renderer_->redraw_overlay();
		}
	}
}

void RendererPrivate::picking_target_changed( size_t viewer_id )
{
	if ( this->viewer_id_ == viewer_id )
	{
		return;
	}
	Core::StateEngine::lock_type lock( Core::StateEngine::GetMutex() );
	ViewerHandle self_viewer = ViewerManager::Instance()->get_viewer( this->viewer_id_ );
	ViewerHandle updated_viewer = ViewerManager::Instance()->get_viewer( viewer_id );
	if ( self_viewer->view_mode_state_->index() !=
		updated_viewer->view_mode_state_->index() )
	{
		this->renderer_->redraw_overlay();
	}
}

void RendererPrivate::draw_slices_3d( const Core::BBox& bbox, 
							  const std::vector< LayerSceneHandle >& layer_scenes, 
							  const std::vector< double >& depths,
							  const std::vector< std::string >& view_modes,
							  bool with_lighting )
{
	this->slice_shader_->enable();
	this->slice_shader_->set_lighting( with_lighting );
	size_t num_of_viewers = layer_scenes.size();
	
	// for each visible 2D viewer
	for ( size_t i = 0; i < num_of_viewers; i++ )
	{
		std::string view_mode = view_modes[ i ];
		double depth = depths[ i ];
		LayerSceneHandle layer_scene = layer_scenes[ i ];
		ProxyRectangleHandle rect( new ProxyRectangle );

		if ( view_mode == Viewer::AXIAL_C )
		{
			rect->bottomleft = bbox.min();
			rect->bottomleft[ 2 ] = depth;

			rect->bottomright[ 0 ] = bbox.max()[ 0 ];
			rect->bottomright[ 1 ] = bbox.min()[ 1 ];
			rect->bottomright[ 2 ] = depth;

			rect->topleft[ 0 ] = bbox.min()[ 0 ];
			rect->topleft[ 1 ] = bbox.max()[ 1 ];
			rect->topleft[ 2 ] = depth;

			rect->topright = bbox.max();
			rect->topright[ 2 ] = depth;

			rect->left = rect->bottomleft[ 0 ];
			rect->right = rect->bottomright[ 0 ];
			rect->bottom = rect->bottomleft[ 1 ];
			rect->top = rect->topleft[ 1 ];
			rect->normal = Core::Vector( 0.0, 0.0, 1.0 );
		}
		else if ( view_mode == Viewer::CORONAL_C )
		{
			rect->bottomleft = bbox.min();
			rect->bottomleft[ 1 ] = depth;

			rect->bottomright[ 0 ] = bbox.max()[ 0 ];
			rect->bottomright[ 2 ] = bbox.min()[ 2 ];
			rect->bottomright[ 1 ] = depth;

			rect->topleft[ 0 ] = bbox.min()[ 0 ];
			rect->topleft[ 2 ] = bbox.max()[ 2 ];
			rect->topleft[ 1 ] = depth;

			rect->topright = bbox.max();
			rect->topright[ 1 ] = depth;

			rect->left = rect->bottomleft[ 0 ];
			rect->right = rect->bottomright[ 0 ];
			rect->bottom = rect->bottomleft[ 2 ];
			rect->top = rect->topleft[ 2 ];
			rect->normal = Core::Vector( 0.0, -1.0, 0.0 );
		}
		else
		{
			rect->bottomleft = bbox.min();
			rect->bottomleft[ 0 ] = depth;

			rect->bottomright[ 1 ] = bbox.max()[ 1 ];
			rect->bottomright[ 2 ] = bbox.min()[ 2 ];
			rect->bottomright[ 0 ] = depth;

			rect->topleft[ 1 ] = bbox.min()[ 1 ];
			rect->topleft[ 2 ] = bbox.max()[ 2 ];
			rect->topleft[ 0 ] = depth;

			rect->topright = bbox.max();
			rect->topright[ 0 ] = depth;

			rect->left = rect->bottomleft[ 1 ];
			rect->right = rect->bottomright[ 1 ];
			rect->bottom = rect->bottomleft[ 2 ];
			rect->top = rect->topleft[ 2 ];
			rect->normal = Core::Vector( 1.0, 0.0, 0.0 );
		}

		for ( size_t layer_num = 0; layer_num < layer_scene->size(); layer_num++ )
		{
			this->draw_slice( ( *layer_scene )[ layer_num ], Core::Matrix::Identity(), rect );
		} // end for

	} // end for each viewer
	this->slice_shader_->disable();
}

void RendererPrivate::draw_slice( LayerSceneItemHandle layer_item, 
						  const Core::Matrix& proj_mat, 
						  ProxyRectangleHandle rect )
{
	this->slice_shader_->set_volume_type( layer_item->type() );
	this->slice_shader_->set_opacity( static_cast< float >( layer_item->opacity_ ) );
	Core::VolumeSlice* volume_slice = layer_item->volume_slice_.get();
	switch ( layer_item->type() )
	{
	case Core::VolumeType::DATA_E:
		{
			DataLayerSceneItem* data_layer_item = 
				dynamic_cast< DataLayerSceneItem* >( layer_item.get() );

			// Convert contrast to range ( 0, 1 ] and brightness to [ -1, 1 ]
			double contrast = ( 1 - data_layer_item->contrast_ / 101 );
			double brightness = data_layer_item->brightness_ / 50 - 1.0;

			// NOTE: The equations for computing scale and bias are as follows:
			//
			// double scale = numeric_range / ( contrast * value_range );
			// double window_max = -brightness * ( value_max - value_min ) + value_max;
			// double bias = ( numeric_max - window_max * scale ) / numeric_max;
			//
			// However, since we always rescale the data to unsigned short when uploading
			// textures, numeric_range and value_range are the same, 
			// and thus the computation can be simplified.

			double scale = 1.0 / contrast;
			double bias = 1.0 - ( 1.0 - brightness ) * scale;
			this->slice_shader_->set_scale_bias( static_cast< float >( scale ), 
				static_cast< float >( bias ) );
		}
		break;
	case Core::VolumeType::MASK_E:
		{
			MaskLayerSceneItem* mask_layer_item = 
				dynamic_cast< MaskLayerSceneItem* >( layer_item.get() );
			if ( rect )
			{
				this->slice_shader_->set_mask_mode( 2 );
			}
			else
			{
				// If mask fill mode is none, force the border width to be at least 1
				if ( mask_layer_item->fill_ == 0 && mask_layer_item->border_ == 0 )
				{
					mask_layer_item->border_ = 1;
				}

				this->slice_shader_->set_mask_mode( mask_layer_item->fill_ );
				this->slice_shader_->set_border_width( mask_layer_item->border_ );
			}
			Core::Color color = PreferencesManager::Instance()->get_color( mask_layer_item->color_ );
			this->slice_shader_->set_mask_color( static_cast< float >( color.r() / 255 ), 
				static_cast< float >( color.g() / 255 ), static_cast< float >( color.b() / 255 ) );
		}
		break;
	default:
		assert( false );
		return;
	} // end switch


	double slice_width = volume_slice->right() - volume_slice->left();
	double slice_height = volume_slice->top() - volume_slice->bottom();
	if ( slice_width == 0.0 || slice_height == 0.0 )
	{
		return;
	}

	Core::TextureHandle slice_tex = volume_slice->get_texture();
	Core::Texture::lock_type slice_tex_lock( slice_tex->get_mutex() );
	slice_tex->bind();

	if ( rect )
	{
		double tex_left = ( rect->left - volume_slice->left() ) / slice_width;
		double tex_right = ( rect->right - volume_slice->right() ) / slice_width + 1.0;
		double tex_bottom = ( rect->bottom - volume_slice->bottom() ) / slice_height;
		double tex_top = ( rect->top - volume_slice->top() ) / slice_height + 1.0;
		glBegin( GL_QUADS );
		glNormal3dv( &rect->normal[ 0 ] );
		glMultiTexCoord2d( GL_TEXTURE0, tex_left, tex_bottom );
		glVertex3dv( &rect->bottomleft[ 0 ] );
		glMultiTexCoord2d( GL_TEXTURE0, tex_right, tex_bottom );
		glVertex3dv( &rect->bottomright[ 0 ] );
		glMultiTexCoord2d( GL_TEXTURE0, tex_right, tex_top );
		glVertex3dv( &rect->topright[ 0 ] );
		glMultiTexCoord2d( GL_TEXTURE0, tex_left, tex_top );
		glVertex3dv( &rect->topleft[ 0 ] );
		glEnd();
	}
	else
	{
		// NOTE: Extend the size of the slice by half voxel size in each direction. 
		// This is to compensate the difference between node centering and cell centering.
		// The volume data uses node centering, but OpenGL texture uses cell centering.
		double voxel_width = ( volume_slice->right() - volume_slice->left() ) 
			/ ( volume_slice->nx() - 1 );
		double voxel_height = ( volume_slice->top() - volume_slice->bottom() )
			/ ( volume_slice->ny() - 1 );
		double left = volume_slice->left() - 0.5 * voxel_width;
		double right = volume_slice->right() + 0.5 * voxel_width;
		double bottom = volume_slice->bottom() - 0.5 * voxel_height;
		double top = volume_slice->top() + 0.5 * voxel_height;
		slice_width = right - left;
		slice_height = top - bottom;

		// Compute the size of the slice on screen
		Core::Vector slice_x( slice_width, 0.0, 0.0 );
		slice_x = proj_mat * slice_x;
		double slice_screen_width = slice_x.x() / 2.0 * this->renderer_->width_;
		double slice_screen_height = slice_height / slice_width * slice_screen_width;
		float pattern_repeats_x = static_cast< float >( slice_screen_width / PATTERN_SIZE_C );
		float pattern_repeats_y = static_cast< float >( slice_screen_height / PATTERN_SIZE_C );
		this->slice_shader_->set_pixel_size( static_cast< float >( 1.0 / slice_screen_width ), 
			static_cast< float >( 1.0 /slice_screen_height ) );
		glBegin( GL_QUADS );
		glMultiTexCoord2f( GL_TEXTURE0, 0.0f, 0.0f );
		glMultiTexCoord2f( GL_TEXTURE1, 0.0f, 0.0f );
		glVertex2d( left, bottom );
		glMultiTexCoord2f( GL_TEXTURE0, 1.0f, 0.0f );
		glMultiTexCoord2f( GL_TEXTURE1, pattern_repeats_x, 0.0f );
		glVertex2d( right, bottom );
		glMultiTexCoord2f( GL_TEXTURE0, 1.0f, 1.0f );
		glMultiTexCoord2f( GL_TEXTURE1, pattern_repeats_x, pattern_repeats_y );
		glVertex2d( right, top );
		glMultiTexCoord2f( GL_TEXTURE0, 0.0f, 1.0f );
		glMultiTexCoord2f( GL_TEXTURE1, 0.0f, pattern_repeats_y );
		glVertex2d( left, top );
		glEnd();
	}

	// NOTE: Always unbind, because we are deleting textures in a separate thread/context.
	// In this case texture binding of the rendering thread won't be reverted to 0, which
	// will cause problem when a new texture with the same ID is generated and bound to
	// this context, because the driver would think it's already bound.
	slice_tex->unbind();
}

void RendererPrivate::enable_rendering( bool enable )
{
	if ( !this->renderer_->is_renderer_thread() )
	{
		this->renderer_->post_renderer_event( boost::bind( 
			&RendererPrivate::enable_rendering, this, enable ) );
		return;
	}

	this->rendering_enabled_ = enable;

	if ( enable )
	{
		this->renderer_->set_redraw_needed();
		this->renderer_->set_redraw_overlay_needed();
		this->renderer_->redraw_all();
	}
}

void RendererPrivate::process_isosurfaces( IsosurfaceArray& isosurfaces )
{
	std::vector< LayerHandle > layers;
	LayerManager::Instance()->get_layers( layers );
	size_t num_of_layers = layers.size();
	for ( size_t i = 0; i < num_of_layers; ++i )
	{
		if ( layers[ i ]->get_type() == Core::VolumeType::MASK_E 
			&& layers[ i ]->is_visible( this->viewer_id_ )  )
		{
			MaskLayer* mask_layer = static_cast< MaskLayer* >( layers[ i ].get() );
			if ( mask_layer->show_isosurface_state_->get() )
			{
				IsosurfaceRecord* iso_record = new IsosurfaceRecord;
				iso_record->color_ = PreferencesManager::Instance()->get_color( 
					mask_layer->color_state_->get() );
				iso_record->isosurface_ = mask_layer->get_isosurface();
				isosurfaces.push_back( IsosurfaceRecordHandle( iso_record ) );
			}
		}
	}
}

void RendererPrivate::draw_isosurfaces( const IsosurfaceArray& isosurfaces, bool with_lighting )
{
	bool use_colormap = false;

	size_t num_of_isosurfaces = isosurfaces.size();
//	glEnable( GL_CULL_FACE );
	this->isosurface_shader_->enable();
	this->isosurface_shader_->set_lighting( with_lighting );
	this->isosurface_shader_->set_use_colormap( use_colormap  );
	for ( size_t i = 0; i < num_of_isosurfaces; ++i )
	{
		Core::IsosurfaceHandle iso = isosurfaces[ i ]->isosurface_;
		if ( !iso )
		{
			continue;
		}
		glColor3d( isosurfaces[ i ]->color_.r() / 255.0, isosurfaces[ i ]->color_.g() / 255.0, 
			isosurfaces[ i ]->color_.b() / 255.0 );
		Core::ColorMapHandle colormap = iso->get_color_map();
		if( use_colormap && iso->get_color_map().get() != 0 )
		{
			float lookup_min, lookup_max;
			colormap->get_lookup_range( lookup_min, lookup_max );
			this->isosurface_shader_->set_min_val( lookup_min );
			this->isosurface_shader_->set_val_range( lookup_max - lookup_min );
			Core::Texture1DHandle colormap_tex = colormap->get_texture();
			Core::Texture::lock_type tex_lock( colormap_tex->get_mutex() );
			colormap_tex->bind();
			iso->redraw( true );
			colormap_tex->unbind();
		}
		else
		{
			iso->redraw( false );
		}
		CORE_CHECK_OPENGL_ERROR();
	}
	this->isosurface_shader_->disable();
//	glDisable( GL_CULL_FACE );
}

void RendererPrivate::draw_orientation_arrows( const Core::View3D& view_3d )
{
	glPushAttrib( GL_VIEWPORT_BIT | GL_TRANSFORM_BIT );

	// Set the viewport to the top right corner of the scene
	int dimension = Core::Min( this->renderer_->width_, this->renderer_->height_ );
	dimension /= 5;
	glViewport( this->renderer_->width_ - dimension, 
		this->renderer_->height_ - dimension, dimension, dimension );

	// NOTE: Clear the depth buffer so the axes will not be occluded.
	glClear( GL_DEPTH_BUFFER_BIT );

	// Compute the orientation of the axes
	Core::Vector eye_dir = view_3d.eyep() - view_3d.lookat();
	eye_dir.normalize();
	eye_dir *= 3.5;

	glMatrixMode( GL_PROJECTION );
	glPushMatrix();
	glLoadIdentity();
	gluPerspective( 45.0f, 1.0, 0.1, 6.0 );

	glMatrixMode( GL_MODELVIEW );
	glPushMatrix();
	glLoadIdentity();
	gluLookAt( eye_dir[ 0 ], eye_dir[ 1 ], eye_dir[ 2 ], 0.0, 0.0, 0.0, view_3d.up().x(),
		view_3d.up().y(), view_3d.up().z() );

	this->orientation_arrows_->draw();

	glPopMatrix();
	glMatrixMode( GL_PROJECTION );
	glPopMatrix();

	glPopAttrib();
}


//////////////////////////////////////////////////////////////////////////
// Implementation of class Renderer
//////////////////////////////////////////////////////////////////////////

Renderer::Renderer( size_t viewer_id ) :
	RendererBase(), 
	ConnectionHandler(), 
	private_( new RendererPrivate )
{
	this->private_->rendering_enabled_ = true;
	this->private_->renderer_ = this;
	this->private_->slice_shader_.reset( new SliceShader );
	this->private_->isosurface_shader_.reset( new IsosurfaceShader );
	this->private_->text_renderer_.reset( new Core::TextRenderer );
	this->private_->viewer_id_ = viewer_id;
}

Renderer::~Renderer()
{
	this->disconnect_all();
}

void Renderer::post_initialize()
{
	glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	glPixelStorei( GL_PACK_ALIGNMENT, 1 );
	glDisable( GL_CULL_FACE );
	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	glDepthFunc( GL_LEQUAL );

	// Set up the lighting parameters
	float white_color[ 4 ] = { 1.0f, 1.0f, 1.0f, 1.0f };
	float gray_color[ 4 ] = { 0.2f, 0.2f, 0.2f, 1.0f };
	glMaterialfv( GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, white_color );
	glMaterialfv( GL_FRONT_AND_BACK, GL_SPECULAR, gray_color );
	glMaterialf( GL_FRONT_AND_BACK, GL_SHININESS, 128 );
	glLightfv( GL_LIGHT0, GL_DIFFUSE, white_color );
	glLightfv( GL_LIGHT0, GL_SPECULAR, white_color );
	glLightModelfv( GL_LIGHT_MODEL_AMBIENT, gray_color );

	{
		// lock the shared render context
		Core::RenderResources::lock_type lock( Core::RenderResources::GetMutex() );

		this->private_->slice_shader_->initialize();
		this->private_->isosurface_shader_->initialize();
		this->private_->pattern_texture_.reset( new Core::Texture2D );
		this->private_->pattern_texture_->set_image( PATTERN_SIZE_C, PATTERN_SIZE_C, 
			GL_ALPHA, MASK_PATTERNS_C, GL_ALPHA, GL_UNSIGNED_BYTE );
		this->private_->text_texture_.reset( new Core::Texture2D );
		this->private_->orientation_arrows_.reset( new OrientationArrows );
	}

	this->private_->slice_shader_->enable();
	this->private_->slice_shader_->set_slice_texture( 0 );
	this->private_->slice_shader_->set_pattern_texture( 1 );
	this->private_->slice_shader_->set_lighting( false );
	this->private_->slice_shader_->disable();

	this->private_->isosurface_shader_->enable();
	this->private_->isosurface_shader_->set_colormap_texture( 0 );
	this->private_->isosurface_shader_->disable();

	Core::Texture::SetActiveTextureUnit( 1 );
	this->private_->pattern_texture_->bind();
	this->private_->pattern_texture_->set_mag_filter( GL_LINEAR );
	this->private_->pattern_texture_->set_min_filter( GL_LINEAR );
	this->private_->pattern_texture_->set_wrap_s( GL_REPEAT );
	this->private_->pattern_texture_->set_wrap_t( GL_REPEAT );
	Core::Texture::SetActiveTextureUnit( 0 );

	ViewerHandle viewer = ViewerManager::Instance()->get_viewer( this->private_->viewer_id_ );

	this->add_connection( viewer->redraw_scene_signal_.connect( 
		boost::bind( &Renderer::redraw_scene, this ) ) );
	this->add_connection( viewer->redraw_overlay_signal_.connect( 
		boost::bind( &Renderer::redraw_overlay, this ) ) );
	this->add_connection( viewer->redraw_all_signal_.connect( 
		boost::bind( &Renderer::redraw_all, this ) ) );


	size_t num_of_viewers = ViewerManager::Instance()->number_of_viewers();
	for ( size_t i = 0; i < num_of_viewers; i++ )
	{
		if ( i != this->private_->viewer_id_ )
		{
			this->add_connection( ViewerManager::Instance()->get_viewer( i )->slice_changed_signal_.
				connect( boost::bind( &RendererPrivate::viewer_slice_changed, this->private_, _1 ) ) );
			this->add_connection( ViewerManager::Instance()->get_viewer( i )->view_mode_state_->
				state_changed_signal_.connect( boost::bind( 
				&RendererPrivate::viewer_mode_changed, this->private_, i ) ) );
		}
	}
	this->add_connection( ViewerManager::Instance()->picking_target_changed_signal_.connect(
		boost::bind( &RendererPrivate::picking_target_changed, this->private_, _1 ) ) );

	this->add_connection( Core::StateEngine::Instance()->pre_load_states_signal_.connect(
		boost::bind( &RendererPrivate::enable_rendering, this->private_, false ) ) );
	this->add_connection( Core::StateEngine::Instance()->post_load_states_signal_.connect(
		boost::bind( &RendererPrivate::enable_rendering, this->private_, true ) ) );
}

bool Renderer::render()
{
	if ( !this->private_->rendering_enabled_ )
	{
		return false;
	}

	// Lock the state engine
	Core::StateEngine::lock_type state_lock( Core::StateEngine::GetMutex() );

	Core::Color bkg_color = PreferencesManager::Instance()->get_background_color();

	glClearColor( bkg_color.r(), bkg_color.g(), bkg_color.b(), 1.0f );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

	CORE_LOG_DEBUG( std::string("Renderer ") + Core::ExportToString( 
		this->private_->viewer_id_ ) + ": starting redraw" );

	glMatrixMode( GL_PROJECTION );
	glLoadIdentity();

	ViewerHandle viewer = ViewerManager::Instance()->get_viewer( this->private_->viewer_id_ );

	if ( viewer->is_volume_view() )
	{
		Core::View3D view3d( viewer->volume_view_state_->get() );
		std::vector< LayerSceneHandle > layer_scenes;
		std::vector< double > depths;
		std::vector< std::string > view_modes;
		bool with_lighting = viewer->volume_light_visible_state_->get();
		bool draw_slices = viewer->volume_slices_visible_state_->get();
		bool draw_isosurfaces = viewer->volume_isosurfaces_visible_state_->get();
		bool show_invisible_slices = viewer->volume_show_invisible_slices_state_->get();
		size_t num_of_viewers = ViewerManager::Instance()->number_of_viewers();
		for ( size_t i = 0; i < num_of_viewers && draw_slices; i++ )
		{
			ViewerHandle other_viewer = ViewerManager::Instance()->get_viewer( i );
			if ( !other_viewer->slice_visible_state_->get() || 
				other_viewer->is_volume_view() ||
				( !show_invisible_slices && !other_viewer->viewer_visible_state_->get() ) )
			{
				continue;
			}
			// Get a snapshot of current layers
			LayerSceneHandle layer_scene = LayerManager::Instance()->compose_layer_scene( i );
			
			// Copy slices from viewer
			this->private_->process_slices( layer_scene, other_viewer );

			if ( layer_scene->size() > 0 )
			{
				layer_scenes.push_back( layer_scene );
				Core::StateView2D* view2d_state = static_cast< Core::StateView2D* >(
					other_viewer->get_active_view_state().get() );
				depths.push_back( view2d_state->get().center().z() );
				view_modes.push_back( other_viewer->view_mode_state_->get() );
			}
		}

		Core::BBox bbox = LayerManager::Instance()->get_layers_bbox();
		IsosurfaceArray isosurfaces;
		if ( draw_isosurfaces )
		{
			this->private_->process_isosurfaces( isosurfaces );
		}		

		// We have got everything we want from the state engine, unlock before we do any rendering
		state_lock.unlock();

		glEnable( GL_DEPTH_TEST );
		glEnable( GL_BLEND );
		glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

		double znear, zfar;
		view3d.compute_clipping_planes( bbox, znear, zfar );
		
		gluPerspective( view3d.fov(), this->width_ / ( 1.0 * this->height_ ), znear, zfar );
		glMatrixMode( GL_MODELVIEW );
		glLoadIdentity();
		gluLookAt( view3d.eyep().x(), view3d.eyep().y(), view3d.eyep().z(), view3d.lookat().x(),
		    view3d.lookat().y(), view3d.lookat().z(), view3d.up().x(), view3d.up().y(), view3d.up().z() );

		if ( draw_slices )
		{
			this->private_->draw_slices_3d( bbox, layer_scenes, depths, view_modes, with_lighting );
		}
		if ( draw_isosurfaces)
		{
			this->private_->draw_isosurfaces( isosurfaces, with_lighting );
		}

		// NOTE: The orientation axes should be drawn the last, because it clears the depth buffer.
		this->private_->draw_orientation_arrows( view3d );
		
		glDisable( GL_BLEND );
	}
	else
	{
		// Get a snapshot of current layers
		LayerSceneHandle layer_scene = LayerManager::Instance()->
			compose_layer_scene( this->private_->viewer_id_ );
		
		// Copy slices from viewer
		this->private_->process_slices( layer_scene, viewer );

		Core::View2D view2d( static_cast< Core::StateView2D* >( 
			viewer->get_active_view_state().get() )->get() );

		// We have got everything we want from the state engine, unlock before we do any rendering
		state_lock.unlock();

		glDisable( GL_DEPTH_TEST );
		glEnable( GL_BLEND );
		glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

		double left, right, top, bottom;
		view2d.compute_clipping_planes( this->width_ / ( 1.0 * this->height_ ), 
			left, right, bottom, top );
		Core::Matrix proj_mat;
		Core::Transform::BuildOrtho2DMatrix( proj_mat, left, right, bottom, top );
		glMultMatrixd( proj_mat.data() );
		glMatrixMode( GL_MODELVIEW );
		glLoadIdentity();

		this->private_->slice_shader_->enable();
		this->private_->slice_shader_->set_lighting( false );

		for ( size_t layer_num = 0; layer_num < layer_scene->size(); layer_num++ )
		{
			this->private_->draw_slice( ( *layer_scene )[ layer_num ], proj_mat );
		} 

		this->private_->slice_shader_->disable();
		glDisable( GL_BLEND );
	}

	CORE_LOG_DEBUG( std::string("Renderer ") + Core::ExportToString( 
		this->private_->viewer_id_ ) + ": done redraw" );

	return true;
}

bool Renderer::render_overlay()
{
	if ( !this->private_->rendering_enabled_ )
	{
		return false;
	}

	glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

	CORE_LOG_DEBUG( std::string("Renderer ") + Core::ExportToString( 
		this->private_->viewer_id_ ) + ": starting redraw overlay" );

	glMatrixMode( GL_PROJECTION );
	glLoadIdentity();

	// Lock the state engine
	Core::StateEngine::lock_type state_lock( Core::StateEngine::GetMutex() );

	ViewerHandle viewer = ViewerManager::Instance()->get_viewer( this->private_->viewer_id_ );

	if ( viewer->is_volume_view() )
	{
		state_lock.unlock();
	}
	else
	{
		bool show_grid = viewer->slice_grid_state_->get();
		bool show_picking_lines = viewer->slice_picking_visible_state_->get();
		bool show_overlay = viewer->overlay_visible_state_->get();
		bool show_slice_num = PreferencesManager::Instance()->show_slice_number_state_->get();
		bool zero_based_slice_numbers = PreferencesManager::Instance()->
			zero_based_slice_numbers_state_->get();
		
		int grid_spacing = PreferencesManager::Instance()->grid_size_state_->get();
		Core::Color bkg_color = PreferencesManager::Instance()->get_background_color();
		Core::View2D view2d( static_cast< Core::StateView2D* > ( 
			viewer->get_active_view_state().get() )->get() );
		std::string view_mode = viewer->view_mode_state_->get();
		ViewerInfoList viewers_info[ 3 ];
		ViewerManager::Instance()->get_2d_viewers_info( viewers_info );

		Core::VolumeSliceHandle active_slice = viewer->get_active_volume_slice();
		std::string slice_str;
		if ( active_slice )
		{
			std::stringstream ss;
			if ( active_slice->out_of_boundary() )
			{
				ss << "- / " << active_slice->number_of_slices();
			}
			else
			{
				if ( zero_based_slice_numbers )
				{
					ss << active_slice->get_slice_number() << " / " << active_slice->number_of_slices();				
				}
				else
				{
					ss << active_slice->get_slice_number() + 1 << " / " << active_slice->number_of_slices();
				}
			}
			slice_str = ss.str();
		}

		// We have got everything we want from the state engine, unlock before we do any rendering
		state_lock.unlock();

		double left, right, top, bottom;
		view2d.compute_clipping_planes( this->width_ / ( 1.0 * this->height_ ), 
			left, right, bottom, top );
		Core::Matrix proj_mat;
		Core::Transform::BuildOrtho2DMatrix( proj_mat, left, right, bottom, top );

		glDisable( GL_DEPTH_TEST );
		// Enable blending
		glEnable( GL_BLEND );
		// NOTE: The result of the following blend function is that, color channels contains
		// colors modulated by alpha, alpha channel stores the value of "1-alpha"
		glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA , 
			GL_ZERO, GL_ONE_MINUS_SRC_ALPHA  );

		gluOrtho2D( 0, this->width_ - 1, 0, this->height_ - 1 );
		glMatrixMode( GL_MODELVIEW );
		glLoadIdentity();

		// Render the active tool
		ToolHandle tool = ToolManager::Instance()->get_active_tool();
		if ( tool )
		{
			tool->redraw( this->private_->viewer_id_, proj_mat );
		}

		// Render the grid
		if ( show_overlay && show_grid )
		{
			glColor4f( 1.0f - bkg_color.r(), 1.0f - bkg_color.g(), 1.0f - bkg_color.b(), 0.75f );
			int center_x = this->width_ / 2;
			int center_y = this->height_ / 2;
			int vertical_lines = center_x / grid_spacing;
			int horizontal_lines = center_y / grid_spacing;
			glBegin( GL_LINES );
			glVertex2i( center_x, 0 );
			glVertex2i( center_x, this->height_ );
			for ( int i = 1; i <= vertical_lines; i++ )
			{
				glVertex2i( center_x - grid_spacing * i, 0 );
				glVertex2i( center_x - grid_spacing * i, this->height_ );
				glVertex2i( center_x + grid_spacing * i, 0 );
				glVertex2i( center_x + grid_spacing * i, this->height_ );
			}
			glVertex2i( 0, center_y );
			glVertex2i( this->width_, center_y );
			for ( int i = 0; i <= horizontal_lines; i++ )
			{
				glVertex2i( 0, center_y - grid_spacing * i );
				glVertex2i( this->width_, center_y - grid_spacing * i );
				glVertex2i( 0, center_y + grid_spacing * i );
				glVertex2i( this->width_, center_y + grid_spacing * i );
			}
			glEnd();
		} // end if ( show_grid )
		
		// Render the positions of slices in other viewers
		if ( show_overlay && show_picking_lines )
		{
			glLineStipple( 1, 0x1C47 );
			int vert_slice_mode;
			int hori_slice_mode;
			if ( view_mode == Viewer::SAGITTAL_C )
			{
				vert_slice_mode = 1;
				hori_slice_mode = 2;
			}
			else if ( view_mode == Viewer::CORONAL_C )
			{
				vert_slice_mode = 0;
				hori_slice_mode = 2;
			}
			else
			{
				vert_slice_mode = 0;
				hori_slice_mode = 1;
			}

			size_t num_of_vert_slices = viewers_info[ vert_slice_mode ].size();
			size_t num_of_hori_slices = viewers_info[ hori_slice_mode ].size();
			for ( size_t i = 0; i < num_of_vert_slices; i++ )
			{
				Core::Point pt( viewers_info[ vert_slice_mode ][ i ]->depth_, 0, 0 );
				pt = proj_mat * pt;
				int slice_pos = Core::Round( ( pt.x() + 1.0 ) / 2.0 * ( this->width_ - 1 ) );
				float color[ 4 ] = { 0.0f, 0.0f, 0.0f, 1.0f };
				color[ vert_slice_mode ] = 1.0f;
				if ( viewers_info[ vert_slice_mode ][ i ]->is_picking_target_ )
				{
					glDisable( GL_LINE_STIPPLE );
				}
				else
				{
					glEnable( GL_LINE_STIPPLE );
					color[ 3 ] = 0.5f;
				}
				//color[ 3 ] = viewers_info[ vert_slice_mode ][ i ]->is_picking_target_ ? 0.75f : 0.3f;
				glColor4fv( color );
				glBegin( GL_LINES );
				glVertex2i( slice_pos, 0 );
				glVertex2i( slice_pos, this->height_ );
				glEnd();
			}
			for ( size_t i = 0; i < num_of_hori_slices; i++ )
			{
				Core::Point pt( 0, viewers_info[ hori_slice_mode ][ i ]->depth_, 0 );
				pt = proj_mat * pt;
				int slice_pos = Core::Round( ( pt.y() + 1.0 ) / 2.0 * ( this->height_ - 1 ) );
				float color[ 4 ] = { 0.0f, 0.0f, 0.0f, 1.0f };
				color[ hori_slice_mode ] = 1.0f;
				if ( viewers_info[ hori_slice_mode ][ i ]->is_picking_target_ )
				{
					glDisable( GL_LINE_STIPPLE );
				}
				else
				{
					glEnable( GL_LINE_STIPPLE );
					color[ 3 ] = 0.5f;
				}
				//color[ 3 ] = viewers_info[ hori_slice_mode ][ i ]->is_picking_target_ ? 0.75f : 0.3f;
				glColor4fv( color );		
				glBegin( GL_LINES );
				glVertex2i( 0, slice_pos);
				glVertex2i( this->width_, slice_pos );
				glEnd();
			}
			glDisable( GL_LINE_STIPPLE );
		} // end if ( show_picking_lines )

		// Render the slice number text
		if ( show_overlay && show_slice_num )
		{
			std::vector< unsigned char > buffer( this->width_ * this->height_, 0 );
			this->private_->text_renderer_->render_aligned( slice_str, &buffer[ 0 ], 
				this->width_, this->height_, 14, Core::TextHAlignmentType::RIGHT_E, 
				Core::TextVAlignmentType::TOP_E, 5, 5, 5, 5 );

			this->private_->text_texture_->enable();
			this->private_->text_texture_->set_sub_image( 0, 0, this->width_, this->height_,
				&buffer[ 0 ], GL_ALPHA, GL_UNSIGNED_BYTE );

			// Blend the text onto the framebuffer
			glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD );
			glBegin( GL_QUADS );
			glColor4f( 1.0f, 0.6f, 0.1f, 0.75f );
			glTexCoord2f( 0.0f, 0.0f );
			glVertex2i( 0, 0 );
			glTexCoord2f( 1.0f, 0.0f );
			glVertex2i( this->width_ - 1, 0 );
			glTexCoord2f( 1.0f, 1.0f );
			glVertex2i( this->width_ - 1, this->height_ - 1 );
			glTexCoord2f( 0.0f, 1.0f );
			glVertex2i( 0, this->height_ - 1 );
			glEnd();
			this->private_->text_texture_->disable();
		} // end rendering text

		glDisable( GL_BLEND );
	}

	CORE_LOG_DEBUG( std::string("Renderer ") + Core::ExportToString( 
		this->private_->viewer_id_ ) + ": done redraw overlay" );

	return true;
}

void Renderer::post_resize()
{
	Core::RenderResources::lock_type lock( Core::RenderResources::GetMutex() );
	this->private_->text_texture_->set_image( this->width_, this->height_, GL_ALPHA );
}

} // end namespace Seg3D