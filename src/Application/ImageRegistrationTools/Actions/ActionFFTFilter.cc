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

#include <Application/ImageRegistrationTools/Actions/ActionFFTFilter.h>

// the includes:
#include <Core/ITKCommon/common.hxx>
#include <Core/ITKCommon/FFT/fft_common.hxx>
#include <Core/ITKCommon/ThreadUtils/the_boost_mutex.hxx>
#include <Core/ITKCommon/ThreadUtils/the_boost_thread.hxx>
#include <Core/ITKCommon/Optimizers/itkRegularStepGradientDescentOptimizer2.h>
#include <Core/ITKCommon/Optimizers/itkImageMosaicVarianceMetric.h>
#include <Core/ITKCommon/Transform/itkLegendrePolynomialTransform.h>
#include <Core/ITKCommon/mosaic_layout_common.hxx>
#include <Core/ITKCommon/the_utils.hxx>

// ITK includes:
#include <itkCommand.h>
#include <itkImageMaskSpatialObject.h>
#include <itkImageRegistrationMethod.h>
#include <itkMeanSquaresImageToImageMetric.h>

#include <Core/Utils/Log.h>

// boost:
#include <boost/filesystem.hpp>

namespace bfs=boost::filesystem;

CORE_REGISTER_ACTION( Seg3D, FFTFilter )

namespace Seg3D
{

//static void
//usage(const char * message = NULL)
//{
//  std::cerr << "USAGE: ir-fft\n"
//  << "\t[-sh shrink_factor] \n"
//  << "\t[-sp pixel_spacing] \n"
//  << "\t[-py number_of_multiresolution_pyramid_levels] \n"
//  << "\t[-std_mask] \n"
//  << "\t[-refine] \n"
//  << "\t[-ol overlap_min overlap_max] \n"
//  << "\t[-clahe slope] \n"
//  << "\t[-debug prefix] \n"
//  << "\t[-data_dir directory] \n"
//  << "\t[-verbose] \n"
//  << "\t-save transform_params_file_out \n"
//  << "\t-data image_0 image_1 [image_n]* \n"
//  << std::endl
//  << "EXAMPLE: ir-fft\n"
//  << "\t-sh 8 \n"
//  << "\t-sp 1 \n"
//  << "\t-py 3 \n"
//  << "\t-ol 0.05 0.5 \n"
//  << "\t-clahe 6 \n"
//  << "\t-save results.txt \n"
//  << "\t-data L.png R.png \n"
//  << std::endl
//  << std::endl
//  << "Pass in -help for more detailed information about the tool."
//  << std::endl;
//  
//  if (message != NULL)
//  {
//    std::cerr << "ERROR: " << message << std::endl;
//  }
//}

bool
ActionFFTFilter::validate( Core::ActionContextHandle& context )
{
  return true;
}

// TODO: dumping original code in here for now,
// should create a proper algorithm class...
bool
ActionFFTFilter::run( Core::ActionContextHandle& context, Core::ActionResultHandle& result )
{
  // this is so that the printouts look better:
  std::cout.precision(6);
  std::cout.setf(std::ios::scientific);
  
  // setup thread storage for the main thread:
  set_the_thread_storage_provider(the_boost_thread_t::thread_storage);
  the_boost_thread_t MAIN_THREAD_DUMMY;
  MAIN_THREAD_DUMMY.set_stopped(false);
  
  // setup thread and mutex interface creators:
  the_mutex_interface_t::set_creator(the_boost_mutex_t::create);
  the_thread_interface_t::set_creator(the_boost_thread_t::create);
  
  // parse the command line arguments:
  std::list<bfs::path> in;
  // TODO: make sure directories are created!!!
  bfs::path fn_results(this->output_mosaic_file_);
  bfs::path directory(this->directory_);

  // TODO: expose
  bool verbose = true;
  
  // by default run as many threads as there are cores:
  unsigned int num_threads = boost::thread::hardware_concurrency();
  fftwf_init_threads();
  itk_fft::set_num_fftw_threads(num_threads);

  // TODO: does it make sense to expose this?
  // for debugging:
  bfs::path prefix;
  if (! bfs::is_directory(directory) )
  {
    std::ostringstream oss;
    oss << directory.string() << " is not a directory.";
    CORE_LOG_ERROR(oss.str());
    return false;
  }

  // populate image list, filter for regular files before attempting to pass entries to ITK
  // providing list of files is not required
  if ( this->files_.empty() )
  {
    bfs::recursive_directory_iterator it(directory);
    bfs::recursive_directory_iterator end;
    for (bfs::recursive_directory_iterator it(directory); it != end; ++it)
    {
      if ( bfs::is_regular_file(*it) )
      {
        in.push_back( it->path() );
      }
    }
  }
  else
  {
    for (unsigned int i = 0; i < this->files_.size(); i++)
    {
      bfs::path image_path = directory / this->files_[i];
      if ( bfs::is_regular_file(image_path) )
      {
        in.push_back(image_path);
      }
    }
  }
  
  
  if (in.size() < 2 && ! this->run_on_one_)
  {
//    usage("must specify at least 2 input images");
    CORE_LOG_ERROR("FFT filter requires at least 2 input images unless explicitly running on one image.");
    return false;
  }
  
  // save the mosaic configuration:
  std::fstream fout;
  fout.open(fn_results.c_str(), std::ios::out);
  if (!fout.is_open())
  {
//    usage(the_text_t("could not open \'") + fn_results +
//          the_text_t("\' for writing"));

    std::ostringstream oss;
    oss << "Could not open " << fn_results.string() << " for writing.";
    CORE_LOG_ERROR(oss.str());
    return false;
  }
  
  // TODO: rewrite to keep a list (of bfs::path) of valid images - that way, no remove op is needed!
  // read the input images:
  int idx = 0;
  std::list<bfs::path> invalid_images;
  std::vector<image_t::Pointer> image(in.size());
  std::vector<mask_t::ConstPointer> mask(in.size());
  {
    std::list<bfs::path>::iterator iter = in.begin();
    for (unsigned int i = 0; i < in.size(); i++, ++iter)
    {
      // read the image:
      std::cout << std::setw(3) << i << " ";
      try {
        image[idx] = std_tile<image_t>(*iter, this->shrink_factor_, this->pixel_spacing_);
        idx++;
      }
      catch (...)
      {
        invalid_images.push_back(*iter);
        std::cout << "Invalid image " << (*iter).string() << " skipping..." << std::endl;
        continue;
      }
      
      if (this->use_standard_mask_)
      {
        mask[idx] = std_mask<image_t>(image[idx]);
      }
      
      if (this->clahe_slope_ > 1.0)
      {
        image[idx] = CLAHE<image_t>(image[idx],
                                    255,
                                    255,
                                    this->clahe_slope_,
                                    256,
                                    0.0,
                                    255.0,
                                    mask[idx]);
      }
    }
  }
  
  std::list<bfs::path>::iterator iter = invalid_images.begin();
  for (; iter != invalid_images.end(); ++iter)
  {
    in.remove((*iter).string());
  }
  
  image.resize(in.size());
  mask.resize(in.size());
  
  if (image.size() < 2 && !this->run_on_one_)
  {
//    usage("FFT requires 2 or more valid image files.");
    CORE_LOG_ERROR("FFT requires 2 or more valid image files.");
    return false;
  }
  
  if ( this->run_on_one_ && image.size() == 1 )
  {
    // Just save out the one image to a file.
    std::vector<affine_transform_t::Pointer> affine(1);
    identity_transform_t::Pointer identity = identity_transform_t::New();
    affine[0] = setup_transform<image_t>(image[0], identity);
    
    save_mosaic<affine_transform_t>
    (fout, this->pixel_spacing_, this->use_standard_mask_, in, affine);
    fout.close();
    return true;
  }
  
  // this is just a reminder:
  const bool images_were_resampled = this->shrink_factor_ != 1;
  
  // establish the correspondences between the images:
  unsigned int num_images = image.size();
  
  // setup the image pyramid:
  unsigned int pyramid_shrink_factor = 1;
  array2d(image_t::Pointer) tile_pyramid(this->pyramid_levels_);
  array2d(mask_t::ConstPointer) mask_pyramid(this->pyramid_levels_);
  tile_pyramid[this->pyramid_levels_ - 1] = image;
  mask_pyramid[this->pyramid_levels_ - 1] = mask;
  for (int i = this->pyramid_levels_ - 2; i >= 0; i--)
  {
    tile_pyramid[i].resize(num_images);
    mask_pyramid[i].resize(num_images);
    pyramid_shrink_factor *= 2;
    
    for (unsigned int j = 0; j < num_images; j++)
    {
      image_t::Pointer img = smooth<image_t>(tile_pyramid[i + 1][j], 2.0);
      img = shrink<image_t>(img, 2);
      tile_pyramid[i][j] = img;
      
      if (mask_pyramid[i + 1][j].GetPointer() != NULL)
      {
        mask_t::ConstPointer msk(shrink<mask_t>(mask_pyramid[i + 1][j], 2));
        mask_pyramid[i][j] = msk;
      }
      
      // FIXME:
//#if 0
//        save<native_image_t>(cast<image_t, native_image_t>(tile_pyramid[i][j]),
//                             the_text_t("pyramid_") +
//                             the_text_t::number(i, 2, '0') +
//                             the_text_t("_") +
//                             the_text_t::number(j, 2, '0') +
//                             the_text_t(".png"));
//#endif
    }
  }
  
  // dump the status:
  std::cout << "shrink min: " << this->shrink_factor_ << std::endl
  << "shrink max: " << (this->shrink_factor_ * pyramid_shrink_factor) << std::endl
  << "pixel spacing: " << this->pixel_spacing_ << std::endl
  << "use std mask: " << this->use_standard_mask_ << std::endl
  //  << "iterations per level: " << iterations_per_level << std::endl
  << "overlap limits: " << this->overlap_min_ << ' ' << overlap_max_ << std::endl
  << std::endl;
  
  // There may be alternative mappings from one image to another via
  // a mapping through any number of intermediate images. Such cascaded
  // mapping can be used to identify and discard false mappings.
  unsigned int max_cascade_len = num_images - 2;
  
  // path[num_intermediate_images][i][j] contains the transforms
  // associated with the specified number of intermediate images
  // that maps image i to image j:
  array3d(translate_transform_t::Pointer) path;
  resize(path, max_cascade_len + 1, num_images, num_images);
  
  // cost[num_intermediate_images][i][j] contains the maximum error metric
  // due to the image mapping between each image in the cascading chain:
  array3d(double) cost;
  resize(cost, max_cascade_len + 1, num_images, num_images);
  
  // initialize the path and cost:
  reset(num_images, max_cascade_len, path, cost);
  
  // setup the initial transform:
  std::vector<affine_transform_t::Pointer> affine(num_images);
  
  image_t::PointType offset_min;
  offset_min[0] = -std::numeric_limits<double>::max();
  offset_min[1] = -std::numeric_limits<double>::max();
  image_t::PointType offset_max;
  offset_max[0] = std::numeric_limits<double>::max();
  offset_max[1] = std::numeric_limits<double>::max();
   
  // try to find matching pairs of images:
  std::vector<bool> matched(num_images);
  std::list<unsigned int> tile_order;
  layout_mosaic<image_t, mask_t>(num_images,
                                 tile_pyramid,
                                 mask_pyramid,
                                 max_cascade_len,
                                 affine,
                                 matched,
                                 tile_order,
                                 cost,
                                 path,
                                 images_were_resampled,
                                 this->use_standard_mask_,
                                 this->overlap_min_,
                                 this->overlap_max_,
                                 offset_min,
                                 offset_max,
                                 this->MAX_PEAKS,
                                 this->try_refining_,
                                 prefix);
  
  // discard unmatched images:
  unsigned int num_unmatched = 0;
  for (unsigned int i = 0; i < num_images; i++)
  {
    if (matched[i]) continue;
    
    std::cerr << "UNMATCHED: " << *iterator_at_index(in, i)
    << ", ignoring..."
    << std::endl;
    image[i] = NULL;
    num_unmatched++;
  }
  std::cerr << std::endl;
  std::cerr << std::endl;
  
  if (num_unmatched == num_images)
  {
    std::cerr << "could not find any matching images, aborting..." << std::endl;
    return false;
  }
  
  // remap the images, forget about the mismatched images:
  affine = remap(tile_order, affine);
  in = remap(tile_order, in);
  image = remap(tile_order, image);
  mask = remap(tile_order, mask);
  num_images = in.size();
  
  save_mosaic<affine_transform_t>(fout, this->pixel_spacing_, this->use_standard_mask_, in, affine);
  fout.close();
  
  // FIXME: this is just for debugging:
  // verify that the mosaic will be built up in the correct order:
  if (! prefix.empty())
  {
    std::vector<bool> omit(num_images);
    omit.assign(num_images, true);
    
    for (unsigned int i = 0; i < num_images; i++)
    {
      std::cout << i << ". assembling mosaic..." << std::endl;
      
      for (unsigned int j = 0; j < i; j++) omit[j] = false;
      
      image_t::Pointer prev =
      make_mosaic<image_t::Pointer, affine_transform_t::Pointer>
      (image[0]->GetSpacing(),
       num_images,
       omit,
       affine,
       image,
       mask,
       FEATHER_NONE_E,
       0);
      
      omit.assign(num_images, true);
      for (unsigned int j = 0; j < i; j++) omit[j] = true;
      omit[i] = false;
      
      image_t::Pointer next =
      make_mosaic<image_t::Pointer, affine_transform_t::Pointer>
      (image[0]->GetSpacing(),
       num_images,
       omit,
       affine,
       image,
       mask,
       FEATHER_NONE_E,
       0);
      
      std::ostringstream filename;
      filename << prefix << "fft-" << the_text_t::number(i, 3, '0') << ".png";
      save_composite<image_t>(bfs::path(filename.str()),
                              prev,
                              next,
                              identity_transform_t::New());
    }
  }
  
  CORE_LOG_SUCCESS("ir-fft done");
  
  // done:
  return true;
}

void
ActionFFTFilter::Dispatch(Core::ActionContextHandle context,
                          std::string target_layer,
                          std::string directory,
                          std::string output_mosaic_file,
                          std::vector<std::string> files,
                          unsigned int shrink_factor,
                          unsigned int pyramid_levels,
                          unsigned int iterations_per_level,
                          double pixel_spacing,
                          double clahe_slope,
                          double overlap_min,
                          double overlap_max,
                          bool use_standard_mask,
                          bool try_refining,
                          bool run_on_one)
{
  // Create a new action
  ActionFFTFilter* action = new ActionFFTFilter;
  
  // Setup the parameters
  // TODO: file & directory mechanism is clumsy - fix this!!!
  action->target_layer_ = target_layer;
  action->files_ = files;
  action->directory_ = directory;
  action->output_mosaic_file_ = output_mosaic_file;
  action->shrink_factor_ = shrink_factor;
  action->pyramid_levels_ = pyramid_levels;
  action->iterations_per_level_ = iterations_per_level;
  action->pixel_spacing_ = pixel_spacing;
  action->clahe_slope_ = clahe_slope;
  action->overlap_min_ = overlap_min;
  action->overlap_max_ = overlap_max;
  action->use_standard_mask_ = use_standard_mask;
  action->try_refining_ = try_refining;
  action->run_on_one_ = run_on_one;
  
  // Dispatch action to underlying engine
  Core::ActionDispatcher::PostAction( Core::ActionHandle( action ), context );
}

}