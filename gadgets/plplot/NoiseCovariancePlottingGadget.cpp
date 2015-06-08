
#include "NoiseCovariancePlottingGadget.h"

#include "GtPLplot.h"

#include "fstream"

#ifndef _WIN32
#include <sys/types.h>
#include <sys/stat.h>
#endif // _WIN32

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/split.hpp>

#include "mri_core_def.h"

namespace Gadgetron{

NoiseCovariancePlottingGadget::NoiseCovariancePlottingGadget()
    : noise_decorrelation_calculated_(false)
    , noise_dwell_time_us_(-1.0f)
    , noiseCovarianceLoaded_(false)
{
    noise_dependency_prefix_ = "GadgetronNoiseCovarianceMatrix";
    measurement_id_.clear();
    measurement_id_of_noise_dependency_.clear();
}

NoiseCovariancePlottingGadget::~NoiseCovariancePlottingGadget()
{

}

int NoiseCovariancePlottingGadget::process_config(ACE_Message_Block* mb)
{
    if (!workingDirectory.value().empty())
    {
        noise_dependency_folder_ = workingDirectory.value();
    }
    else
    {
#ifdef _WIN32
        noise_dependency_folder_ = std::string("c:\\temp\\gadgetron\\");
#else
        noise_dependency_folder_ =  std::string("/tmp/gadgetron/");
#endif // _WIN32
    }

    GDEBUG("Folder to store noise dependencies is %s\n", noise_dependency_folder_.c_str());

    if (!noise_dependency_prefix.value().empty()) noise_dependency_prefix_ = noise_dependency_prefix.value();

    ISMRMRD::deserialize(mb->rd_ptr(), current_ismrmrd_header_);

    // find the measurementID of this scan
    if (current_ismrmrd_header_.measurementInformation)
    {
        if (current_ismrmrd_header_.measurementInformation->measurementID)
        {
            measurement_id_ = *current_ismrmrd_header_.measurementInformation->measurementID;
            GDEBUG("Measurement ID is %s\n", measurement_id_.c_str());
        }

        // find the noise depencies if any
        if (current_ismrmrd_header_.measurementInformation->measurementDependency.size() > 0)
        {
            measurement_id_of_noise_dependency_.clear();

            std::vector<ISMRMRD::MeasurementDependency>::const_iterator iter = current_ismrmrd_header_.measurementInformation->measurementDependency.begin();
            for (; iter != current_ismrmrd_header_.measurementInformation->measurementDependency.end(); iter++)
            {
                std::string dependencyType = iter->dependencyType;
                std::string dependencyID = iter->measurementID;

                GDEBUG("Found dependency measurement : %s with ID %s\n", dependencyType.c_str(), dependencyID.c_str());

                if (dependencyType == "Noise" || dependencyType == "noise") {
                    measurement_id_of_noise_dependency_ = dependencyID;
                }
            }

            if (!measurement_id_of_noise_dependency_.empty())
            {
                GDEBUG("Measurement ID of noise dependency is %s\n", measurement_id_of_noise_dependency_.c_str());

                full_name_stored_noise_dependency_ = this->generateNoiseDependencyFilename(generateMeasurementIdOfNoiseDependency(measurement_id_of_noise_dependency_));
                GDEBUG("Stored noise dependency is %s\n", full_name_stored_noise_dependency_.c_str());
            }
        }
        else
        {
            full_name_stored_noise_dependency_ = this->generateNoiseDependencyFilename(measurement_id_);
            GDEBUG("Stored noise dependency is %s\n", full_name_stored_noise_dependency_.c_str());
        }
    }

    return GADGET_OK;
}

std::string NoiseCovariancePlottingGadget::generateMeasurementIdOfNoiseDependency(const std::string& noise_id)
{
    // find the scan prefix
    std::string measurementStr = measurement_id_;
    size_t ind = measurement_id_.find_last_of("_");
    if (ind != std::string::npos) {
        measurementStr = measurement_id_.substr(0, ind);
        measurementStr.append("_");
        measurementStr.append(noise_id);
    }

    return measurementStr;
}

std::string NoiseCovariancePlottingGadget::generateNoiseDependencyFilename(const std::string& measurement_id)
{
    std::string full_name_stored_noise_dependency;

    full_name_stored_noise_dependency = noise_dependency_folder_;
    full_name_stored_noise_dependency.append("/");
    full_name_stored_noise_dependency.append(noise_dependency_prefix_);
    full_name_stored_noise_dependency.append("_");
    full_name_stored_noise_dependency.append(measurement_id);

    return full_name_stored_noise_dependency;
}

bool NoiseCovariancePlottingGadget::loadNoiseCovariance()
{
    std::ifstream infile;
    infile.open(full_name_stored_noise_dependency_.c_str(), std::ios::in | std::ios::binary);

    if (infile.good())
    {
        //Read the XML header of the noise scan
        uint32_t xml_length;
        infile.read(reinterpret_cast<char*>(&xml_length), 4);
        std::string xml_str(xml_length, '\0');
        infile.read(const_cast<char*>(xml_str.c_str()), xml_length);
        ISMRMRD::deserialize(xml_str.c_str(), noise_ismrmrd_header_);

        infile.read(reinterpret_cast<char*>(&noise_dwell_time_us_), sizeof(float));

        size_t len;
        infile.read(reinterpret_cast<char*>(&len), sizeof(size_t));

        char* buf = new char[len];
        if (buf == NULL) return false;

        infile.read(buf, len);

        if (!noise_covariance_matrixf_.deserialize(buf, len))
        {
            delete[] buf;
            return false;
        }

        delete[] buf;
        infile.close();
    }
    else
    {
        return false;
    }

    return true;
}

int NoiseCovariancePlottingGadget::process(GadgetContainerMessage<ISMRMRD::AcquisitionHeader>* m1, GadgetContainerMessage< hoNDArray< std::complex<float> > >* m2)
{
    m1->release();
    return GADGET_OK;
}

int NoiseCovariancePlottingGadget::close(unsigned long flags)
{
    if (BaseClass::close(flags) != GADGET_OK) return GADGET_FAIL;

    // try to load the precomputed noise prewhitener
    if (flags == 0) return GADGET_OK;

    if (!full_name_stored_noise_dependency_.empty())
    {
        if (this->loadNoiseCovariance())
        {
            GDEBUG("Stored noise dependency is found : %s\n", full_name_stored_noise_dependency_.c_str());

            if (noise_ismrmrd_header_.acquisitionSystemInformation.is_present())
            {
                hoNDArray<float> plotIm;

                bool trueColor = false;
                size_t CHA = noise_covariance_matrixf_.get_size(0);

                std::vector<std::string> coilStrings(CHA, "Unknown");
                for (size_t n = 0; n < CHA; n++)
                {
                    int coilNum = noise_ismrmrd_header_.acquisitionSystemInformation.get().coilLabel[n].coilNumber;
                    if (coilNum >= 0 && coilNum < CHA)
                    {
                        coilStrings[coilNum] = noise_ismrmrd_header_.acquisitionSystemInformation.get().coilLabel[n].coilName;
                    }
                }

                Gadgetron::plotNoiseStandardDeviation(noise_covariance_matrixf_, coilStrings, xlabel.value(), ylabel.value(), title.value(), xsize.value(), ysize.value(), trueColor, plotIm);

                // send out the noise variance plot
                Gadgetron::GadgetContainerMessage<ISMRMRD::ImageHeader>* cm1 = new Gadgetron::GadgetContainerMessage<ISMRMRD::ImageHeader>();
                Gadgetron::GadgetContainerMessage<ISMRMRD::MetaContainer>* cm3 = new Gadgetron::GadgetContainerMessage<ISMRMRD::MetaContainer>();

                cm1->getObjectPtr()->flags = 0;
                cm1->getObjectPtr()->data_type = ISMRMRD::ISMRMRD_FLOAT;
                cm1->getObjectPtr()->image_type = ISMRMRD::ISMRMRD_IMTYPE_MAGNITUDE;

                cm1->getObjectPtr()->image_index = 1;
                cm1->getObjectPtr()->image_series_index = series_num.value();

                Gadgetron::GadgetContainerMessage< Gadgetron::hoNDArray<float> >* cm2 = new Gadgetron::GadgetContainerMessage< Gadgetron::hoNDArray<float> >();
                cm1->cont(cm2);
                cm2->cont(cm3);

                std::vector<size_t> img_dims(2);
                img_dims[0] = plotIm.get_size(0);
                img_dims[1] = plotIm.get_size(1);

                // set the image attributes
                cm3->getObjectPtr()->set(GADGETRON_IMAGECOMMENT, "Noise SD");
                cm3->getObjectPtr()->set(GADGETRON_SEQUENCEDESCRIPTION, "_GT_Noise_SD_Plot");
                cm3->getObjectPtr()->set(GADGETRON_IMAGEPROCESSINGHISTORY, "GT");
                cm3->getObjectPtr()->set(GADGETRON_DATA_ROLE, GADGETRON_IMAGE_RECON_FIGURE);

                cm3->getObjectPtr()->set(GADGETRON_IMAGE_SCALE_RATIO, (double)(1));
                cm3->getObjectPtr()->set(GADGETRON_IMAGE_WINDOWCENTER, (long)(200));
                cm3->getObjectPtr()->set(GADGETRON_IMAGE_WINDOWWIDTH, (long)(400));

                cm1->getObjectPtr()->matrix_size[0] = (uint16_t)plotIm.get_size(0);
                cm1->getObjectPtr()->matrix_size[1] = (uint16_t)plotIm.get_size(1);
                cm1->getObjectPtr()->matrix_size[2] = 1;
                cm1->getObjectPtr()->channels = 1;

                cm1->getObjectPtr()->field_of_view[0] = 400;
                cm1->getObjectPtr()->field_of_view[1] = 400;
                cm1->getObjectPtr()->field_of_view[2] = 8;

                cm1->getObjectPtr()->position[0] = 0;
                cm1->getObjectPtr()->position[1] = 0;
                cm1->getObjectPtr()->position[2] = 0;

                cm1->getObjectPtr()->patient_table_position[0] = 0;
                cm1->getObjectPtr()->patient_table_position[1] = 0;
                cm1->getObjectPtr()->patient_table_position[2] = 0;

                cm1->getObjectPtr()->read_dir[0] = 1;
                cm1->getObjectPtr()->read_dir[1] = 0;
                cm1->getObjectPtr()->read_dir[2] = 0;

                cm1->getObjectPtr()->phase_dir[0] = 0;
                cm1->getObjectPtr()->phase_dir[1] = 1;
                cm1->getObjectPtr()->phase_dir[2] = 0;

                cm1->getObjectPtr()->slice_dir[0] = 0;
                cm1->getObjectPtr()->slice_dir[1] = 0;
                cm1->getObjectPtr()->slice_dir[2] = 1;

                // give arbitrary time for the figure
                cm1->getObjectPtr()->acquisition_time_stamp = 21937963;
                cm1->getObjectPtr()->physiology_time_stamp[0] = 2;

                try
                {
                    cm2->getObjectPtr()->create(&img_dims);
                }
                catch (...)
                {
                    GDEBUG("Unable to allocate new image\n");
                    cm1->release();
                    return false;
                }

                memcpy(cm2->getObjectPtr()->begin(), plotIm.begin(), sizeof(float)*plotIm.get_size(0)*plotIm.get_size(1));

                // send out the images
                if (this->next()->putq(cm1) < 0)
                {
                    return false;
                }
            }
        }
        else
        {
            GDEBUG("Stored noise dependency is NOT found : %s\n", full_name_stored_noise_dependency_.c_str());
        }
    }
    else
    {
        GDEBUG("There is no noise stored ... \n");
    }

    return GADGET_OK;
}

GADGET_FACTORY_DECLARE(NoiseCovariancePlottingGadget)

} // namespace Gadgetron
