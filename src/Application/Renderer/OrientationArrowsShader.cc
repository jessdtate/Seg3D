/*
 For more information, please see: http://software.sci.utah.edu

 The MIT License

 Copyright (c) 2016 Scientific Computing and Imaging Institute,
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

#include <Core/Utils/Log.h>
#include <Application/Renderer/OrientationArrowsShader.h>

namespace Seg3D
{

OrientationArrowsShader::OrientationArrowsShader() :
  ShaderBase()
{
}

OrientationArrowsShader::~OrientationArrowsShader()
{
}

bool OrientationArrowsShader::get_vertex_shader_source( std::string& source )
{
  const char VERT_SHADER_SOURCE_C[] =
  {
#include "OrientationArrowsShader_vert"
#include "Lighting_vert"
  };
  source = std::string( VERT_SHADER_SOURCE_C );
  return true;
  }

bool OrientationArrowsShader::get_fragment_shader_source( std::string& source )
  {
  const char FRAG_SHADER_SOURCE_C[] =
  {
#include "OrientationArrowsShader_frag"
#include "Lighting_frag"
  };
  source = std::string( FRAG_SHADER_SOURCE_C );
  return true;
}

} // end namespace Seg3D
