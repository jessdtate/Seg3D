/*
 For more information, please see: http://software.sci.utah.edu
 
 The MIT License
 
 Copyright (c) 2013 Scientific Computing and Imaging Institute,
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

#ifndef APPLICATION_BACKSCATTERRECONSTRUCTION_ACTIONS_ACTIONRECONSTRUCTIONVIEW_H
#define APPLICATION_BACKSCATTERRECONSTRUCTION_ACTIONS_ACTIONRECONSTRUCTIONVIEW_H


// Core includes
#include <Core/Action/Action.h>
#include <Core/Interface/Interface.h>


namespace Seg3D
{
  
class ActionReconstructionView : public Core::Action
{
  
CORE_ACTION(
  CORE_ACTION_TYPE( "ActionReconstructionView", "" )
  CORE_ACTION_OPTIONAL_ARGUMENT("outputDir", "", "Algorithm output directory.")
  CORE_ACTION_OPTIONAL_ARGUMENT( "iterations", "50", "Number of iterations to perform." )
  CORE_ACTION_CHANGES_PROJECT_DATA()
)
  
  // -- Constructor/Destructor --
public:
  ActionReconstructionView();

  // -- Functions that describe action --
public:
  virtual bool validate( Core::ActionContextHandle& context );
  virtual bool run( Core::ActionContextHandle& context, Core::ActionResultHandle& result );
  
  // -- Dispatch this action from the interface --
public:
  /// DISPATCH:
  /// Dispatch an action that saves the preferences
  static void Dispatch( Core::ActionContextHandle context );

private:
  int iterations_;
  std::string outputDir_;
  
};
  
} // end namespace Seg3D

#endif