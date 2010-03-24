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

#include <Application/Tool/ToolFactory.h>
#include <Application/Tools/PaintTool.h>
#include <Application/LayerManager/LayerManager.h>

namespace Seg3D
{

// Register the tool into the tool factory
SCI_REGISTER_TOOL(PaintTool)

PaintTool::PaintTool( const std::string& toolid ) :
	Tool( toolid )
{
	// Need to set ranges and default values for all parameters
	add_state( "target", target_layer_state_, "<none>", "<none>" );
	add_state( "mask", mask_layer_state_, "<none>", "<none>" );
	add_state( "brush_radius", brush_radius_state_, 23, 1, 250, 1 );
	add_state( "upper_threshold", upper_threshold_state_, 1.0, 00.0, 1.0, 0.01 );
	add_state( "lower_threshold", lower_threshold_state_, 0.0, 00.0, 1.0, 0.01 );
	add_state( "erase", erase_state_, false );

	// Add constaints, so that when the state changes the right ranges of
	// parameters are selected
	target_layer_state_->value_changed_signal_.connect( boost::bind( &PaintTool::target_constraint,
	    this, _1 ) );
	mask_layer_state_->value_changed_signal_.connect( boost::bind( &PaintTool::mask_constraint,
	    this, _1 ) );

	// If a layer is added or deleted update the lists
	LayerManager::Instance()->layers_finished_deleting_signal_.connect(
	    boost::bind( &PaintTool::handle_layers_changed, this ) );
	
	LayerManager::Instance()->layer_inserted_signal_.connect(
	    boost::bind( &PaintTool::handle_layers_changed, this ) );

	// Trigger a fresh update
//	handle_layers_changed();
}

PaintTool::~PaintTool()
{
	disconnect_all();
}

void PaintTool::handle_layers_changed()
{
    std::vector< LayerHandle > target_layers;
    LayerManager::Instance()->return_layers_vector( target_layers );
    
    //TODO need to filter out the layers that are not valid for target
    //target_layer_state_->set_option_list(target_layers);

	/*
	 std::vector<std::string> target_layers;
	 LayerManager::instance()->get_layers(LayerManager::MASKLAYER_E|
	 LayerManager::ACTIVE_E|
	 LayerManager::NONE_E,
	 target_layers );

	 target_layer_->set_option_list(target_layers);

	 std::vector<std::string> mask_layers;
	 LayerManager::instance()->get_layers(LayerManager::MASKLAYER_E|
	 LayerManager::DATALAYER_E|
	 LayerManager::NONE_E,
	 mask_layers );

	 mask_layer_->set_option_list(mask_layers);
	 */
}

void PaintTool::target_constraint( std::string layerid )
{
}

void PaintTool::mask_constraint( std::string layerid )
{
	/*
	 if (layerid == "<none>")
	 {

	 }
	 else
	 {
	 LayerHandle layer;
	 LayerManager::instance()->get_layer(layerid,layer);
	 }
	 */
}

void PaintTool::activate()
{
}

void PaintTool::deactivate()
{
}

} // end namespace Seg3D
