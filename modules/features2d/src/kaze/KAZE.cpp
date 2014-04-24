
//=============================================================================
//
// KAZE.cpp
// Author: Pablo F. Alcantarilla
// Institution: University d'Auvergne
// Address: Clermont Ferrand, France
// Date: 21/01/2012
// Email: pablofdezalc@gmail.com
//
// KAZE Features Copyright 2012, Pablo F. Alcantarilla
// All Rights Reserved
// See LICENSE for the license information
//=============================================================================

/**
 * @file KAZE.cpp
 * @brief Main class for detecting and describing features in a nonlinear
 * scale space
 * @date Jan 21, 2012
 * @author Pablo F. Alcantarilla
 */

#include "KAZE.h"

// Namespaces
using namespace std;
using namespace cv;

//*******************************************************************************
//*******************************************************************************

/**
 * @brief KAZE constructor with input options
 * @param options KAZE configuration options
 * @note The constructor allocates memory for the nonlinear scale space
 */
KAZEFeatures::KAZEFeatures(KAZEOptions& options) {

    soffset_ = options.soffset;
    sderivatives_ = options.sderivatives;
    omax_ = options.omax;
    nsublevels_ = options.nsublevels;
    save_scale_space_ = options.save_scale_space;
    verbosity_ = options.verbosity;
    img_width_ = options.img_width;
    img_height_ = options.img_height;
    dthreshold_ = options.dthreshold;
    diffusivity_ = options.diffusivity;
    descriptor_mode_ = options.descriptor;
    use_fed_ = options.use_fed;
    use_upright_ = options.upright;
    use_extended_ = options.extended;
    kcontrast_ = DEFAULT_KCONTRAST;
    ncycles_ = 0;
    reordering_ = true;
    tkcontrast_ = 0.0;
    tnlscale_ = 0.0;
    tdetector_ = 0.0;
    tmderivatives_ = 0.0;
    tdresponse_ = 0.0;
    tdescriptor_ = 0.0;

    // Now allocate memory for the evolution
    Allocate_Memory_Evolution();
}

//*******************************************************************************
//*******************************************************************************

/**
 * @brief KAZE destructor
 */
KAZEFeatures::~KAZEFeatures(void) {

    evolution_.clear();
}

//*******************************************************************************
//*******************************************************************************

/**
 * @brief This method allocates the memory for the nonlinear diffusion evolution
 */
void KAZEFeatures::Allocate_Memory_Evolution(void) {

    // Allocate the dimension of the matrices for the evolution
    for (int i = 0; i <= omax_ - 1; i++) {
        for (int j = 0; j <= nsublevels_ - 1; j++) {

            TEvolution aux;
            aux.Lx = cv::Mat::zeros(img_height_, img_width_, CV_32F);
            aux.Ly = cv::Mat::zeros(img_height_, img_width_, CV_32F);
            aux.Lxx = cv::Mat::zeros(img_height_, img_width_, CV_32F);
            aux.Lxy = cv::Mat::zeros(img_height_, img_width_, CV_32F);
            aux.Lyy = cv::Mat::zeros(img_height_, img_width_, CV_32F);
            aux.Lflow = cv::Mat::zeros(img_height_, img_width_, CV_32F);
            aux.Lt = cv::Mat::zeros(img_height_, img_width_, CV_32F);
            aux.Lsmooth = cv::Mat::zeros(img_height_, img_width_, CV_32F);
            aux.Lstep = cv::Mat::zeros(img_height_, img_width_, CV_32F);
            aux.Ldet = cv::Mat::zeros(img_height_, img_width_, CV_32F);
            aux.esigma = soffset_*pow((float)2.0, (float)(j) / (float)(nsublevels_)+i);
            aux.etime = 0.5*(aux.esigma*aux.esigma);
            aux.sigma_size = fRound(aux.esigma);
            aux.octave = i;
            aux.sublevel = j;
            evolution_.push_back(aux);
        }
    }

    // Allocate memory for the FED number of cycles and time steps
    if (use_fed_) {
        for (size_t i = 1; i < evolution_.size(); i++) {
            int naux = 0;
            vector<float> tau;
            float ttime = 0.0;
            ttime = evolution_[i].etime - evolution_[i - 1].etime;
            naux = fed_tau_by_process_time(ttime, 1, 0.25, reordering_, tau);
            nsteps_.push_back(naux);
            tsteps_.push_back(tau);
            ncycles_++;
        }
    }
    else {
        // Allocate memory for the auxiliary variables that are used in the AOS scheme
        Ltx_ = Mat::zeros(img_width_, img_height_, CV_32F);
        Lty_ = Mat::zeros(img_height_, img_width_, CV_32F);
        px_ = Mat::zeros(img_height_, img_width_, CV_32F);
        py_ = Mat::zeros(img_height_, img_width_, CV_32F);
        ax_ = Mat::zeros(img_height_, img_width_, CV_32F);
        ay_ = Mat::zeros(img_height_, img_width_, CV_32F);
        bx_ = Mat::zeros(img_height_ - 1, img_width_, CV_32F);
        by_ = Mat::zeros(img_height_ - 1, img_width_, CV_32F);
        qr_ = Mat::zeros(img_height_ - 1, img_width_, CV_32F);
        qc_ = Mat::zeros(img_height_, img_width_ - 1, CV_32F);
    }

}

//*******************************************************************************
//*******************************************************************************

/**
 * @brief This method creates the nonlinear scale space for a given image
 * @param img Input image for which the nonlinear scale space needs to be created
 * @return 0 if the nonlinear scale space was created successfully. -1 otherwise
 */
int KAZEFeatures::Create_Nonlinear_Scale_Space(const cv::Mat &img) {

    double t2 = 0.0, t1 = 0.0;

    if (evolution_.size() == 0) {
        cout << "Error generating the nonlinear scale space!!" << endl;
        cout << "Firstly you need to call KAZE::Allocate_Memory_Evolution()" << endl;
        return -1;
    }

    t1 = getTickCount();

    // Copy the original image to the first level of the evolution
    img.copyTo(evolution_[0].Lt);
    gaussian_2D_convolution(evolution_[0].Lt, evolution_[0].Lt, 0, 0, soffset_);
    gaussian_2D_convolution(evolution_[0].Lt, evolution_[0].Lsmooth, 0, 0, sderivatives_);

    // Firstly compute the kcontrast factor
    Compute_KContrast(evolution_[0].Lt, KCONTRAST_PERCENTILE);

    t2 = getTickCount();
    tkcontrast_ = 1000.0*(t2 - t1) / getTickFrequency();

    if (verbosity_ == true) {
        cout << "Computed image evolution step. Evolution time: " << evolution_[0].etime <<
            " Sigma: " << evolution_[0].esigma << endl;
    }

    // Now generate the rest of evolution levels
    for (size_t i = 1; i < evolution_.size(); i++) {

        evolution_[i - 1].Lt.copyTo(evolution_[i].Lt);
        gaussian_2D_convolution(evolution_[i - 1].Lt, evolution_[i].Lsmooth, 0, 0, sderivatives_);

        // Compute the Gaussian derivatives Lx and Ly
        Scharr(evolution_[i].Lsmooth, evolution_[i].Lx, CV_32F, 1, 0, 1, 0, BORDER_DEFAULT);
        Scharr(evolution_[i].Lsmooth, evolution_[i].Ly, CV_32F, 0, 1, 1, 0, BORDER_DEFAULT);

        // Compute the conductivity equation
        if (diffusivity_ == 0) {
            pm_g1(evolution_[i].Lx, evolution_[i].Ly, evolution_[i].Lflow, kcontrast_);
        }
        else if (diffusivity_ == 1) {
            pm_g2(evolution_[i].Lx, evolution_[i].Ly, evolution_[i].Lflow, kcontrast_);
        }
        else if (diffusivity_ == 2) {
            weickert_diffusivity(evolution_[i].Lx, evolution_[i].Ly, evolution_[i].Lflow, kcontrast_);
        }

        // Perform FED n inner steps
        if (use_fed_) {
            for (int j = 0; j < nsteps_[i - 1]; j++) {
                nld_step_scalar(evolution_[i].Lt, evolution_[i].Lflow, evolution_[i].Lstep, tsteps_[i - 1][j]);
            }
        }
        else {
            // Perform the evolution step with AOS
            AOS_Step_Scalar(evolution_[i].Lt, evolution_[i - 1].Lt, evolution_[i].Lflow,
                evolution_[i].etime - evolution_[i - 1].etime);
        }

        if (verbosity_ == true) {
            cout << "Computed image evolution step " << i << " Evolution time: " << evolution_[i].etime <<
                " Sigma: " << evolution_[i].esigma << endl;
        }
    }

    t2 = getTickCount();
    tnlscale_ = 1000.0*(t2 - t1) / getTickFrequency();

    return 0;
}

//*************************************************************************************
//*************************************************************************************

/**
 * @brief This method computes the k contrast factor
 * @param img Input image
 * @param kpercentile Percentile of the gradient histogram
 */
void KAZEFeatures::Compute_KContrast(const cv::Mat &img, const float &kpercentile) {

    if (verbosity_ == true) {
        cout << "Computing Kcontrast factor." << endl;
    }

    if (COMPUTE_KCONTRAST == true) {
        kcontrast_ = compute_k_percentile(img, kpercentile, sderivatives_, KCONTRAST_NBINS, 0, 0);
    }

    if (verbosity_ == true) {
        cout << "kcontrast = " << kcontrast_ << endl;
        cout << endl << "Now computing the nonlinear scale space!!" << endl;
    }
}

//*************************************************************************************
//*************************************************************************************

/**
 * @brief This method computes the multiscale derivatives for the nonlinear scale space
 */
void KAZEFeatures::Compute_Multiscale_Derivatives(void)
{
    double t2 = 0.0, t1 = 0.0;
    t1 = getTickCount();

#ifdef _OPENMP
#pragma omp parallel for
#endif
    for (size_t i = 0; i < evolution_.size(); i++) {

        if (verbosity_ == true) {
            cout << "Computing multiscale derivatives. Evolution time: " << evolution_[i].etime
                << " Step (pixels): " << evolution_[i].sigma_size << endl;
        }

        // Compute multiscale derivatives for the detector
        compute_scharr_derivatives(evolution_[i].Lsmooth, evolution_[i].Lx, 1, 0, evolution_[i].sigma_size);
        compute_scharr_derivatives(evolution_[i].Lsmooth, evolution_[i].Ly, 0, 1, evolution_[i].sigma_size);
        compute_scharr_derivatives(evolution_[i].Lx, evolution_[i].Lxx, 1, 0, evolution_[i].sigma_size);
        compute_scharr_derivatives(evolution_[i].Ly, evolution_[i].Lyy, 0, 1, evolution_[i].sigma_size);
        compute_scharr_derivatives(evolution_[i].Lx, evolution_[i].Lxy, 0, 1, evolution_[i].sigma_size);

        evolution_[i].Lx = evolution_[i].Lx*((evolution_[i].sigma_size));
        evolution_[i].Ly = evolution_[i].Ly*((evolution_[i].sigma_size));
        evolution_[i].Lxx = evolution_[i].Lxx*((evolution_[i].sigma_size)*(evolution_[i].sigma_size));
        evolution_[i].Lxy = evolution_[i].Lxy*((evolution_[i].sigma_size)*(evolution_[i].sigma_size));
        evolution_[i].Lyy = evolution_[i].Lyy*((evolution_[i].sigma_size)*(evolution_[i].sigma_size));
    }

    t2 = getTickCount();
    tmderivatives_ = 1000.0*(t2 - t1) / getTickFrequency();
}

//*************************************************************************************
//*************************************************************************************

/**
 * @brief This method computes the feature detector response for the nonlinear scale space
 * @note We use the Hessian determinant as feature detector
 */
void KAZEFeatures::Compute_Detector_Response(void) {

    double t2 = 0.0, t1 = 0.0;
    float lxx = 0.0, lxy = 0.0, lyy = 0.0;

    t1 = getTickCount();

    // Firstly compute the multiscale derivatives
    Compute_Multiscale_Derivatives();

    for (size_t i = 0; i < evolution_.size(); i++) {

        // Determinant of the Hessian
        if (verbosity_ == true) {
            cout << "Computing detector response. Determinant of Hessian. Evolution time: " << evolution_[i].etime << endl;
        }

        for (int ix = 0; ix < img_height_; ix++) {
            for (int jx = 0; jx < img_width_; jx++) {
                lxx = *(evolution_[i].Lxx.ptr<float>(ix)+jx);
                lxy = *(evolution_[i].Lxy.ptr<float>(ix)+jx);
                lyy = *(evolution_[i].Lyy.ptr<float>(ix)+jx);
                *(evolution_[i].Ldet.ptr<float>(ix)+jx) = (lxx*lyy - lxy*lxy);
            }
        }
    }

    t2 = getTickCount();
    tdresponse_ = 1000.0*(t2 - t1) / getTickFrequency();
}

//*************************************************************************************
//*************************************************************************************

/**
 * @brief This method selects interesting keypoints through the nonlinear scale space
 * @param kpts Vector of keypoints
 */
void KAZEFeatures::Feature_Detection(std::vector<cv::KeyPoint>& kpts) {

    double t2 = 0.0, t1 = 0.0;
    t1 = getTickCount();

    kpts.clear();

    // Firstly compute the detector response for each pixel and scale level
    Compute_Detector_Response();

    // Find scale space extrema
    Determinant_Hessian_Parallel(kpts);

    // Perform some subpixel refinement
    Do_Subpixel_Refinement(kpts);

    t2 = getTickCount();
    tdetector_ = 1000.0*(t2 - t1) / getTickFrequency();
}

//*************************************************************************************
//*************************************************************************************

/**
 * @brief This method performs the detection of keypoints by using the normalized
 * score of the Hessian determinant through the nonlinear scale space
 * @param kpts Vector of keypoints
 * @note We compute features for each of the nonlinear scale space level in a different processing thread
 */
void KAZEFeatures::Determinant_Hessian_Parallel(std::vector<cv::KeyPoint>& kpts) {

    int level = 0;
    float dist = 0.0, smax = 3.0;
    int npoints = 0, id_repeated = 0;
    int left_x = 0, right_x = 0, up_y = 0, down_y = 0;
    bool is_extremum = false, is_repeated = false, is_out = false;

    // Delete the memory of the vector of keypoints vectors
    // In case we use the same kaze object for multiple images
    for (size_t i = 0; i < kpts_par_.size(); i++) {
        vector<KeyPoint>().swap(kpts_par_[i]);
    }
    kpts_par_.clear();
    vector<KeyPoint> aux;

    // Allocate memory for the vector of vectors
    for (size_t i = 1; i < evolution_.size() - 1; i++) {
        kpts_par_.push_back(aux);
    }

#ifdef _OPENMP
#pragma omp parallel for
#endif
    for (size_t i = 1; i < evolution_.size() - 1; i++) {
        Find_Extremum_Threading(i);
    }

    // Now fill the vector of keypoints!!!
    for (size_t i = 0; i < kpts_par_.size(); i++) {
        for (size_t j = 0; j < kpts_par_[i].size(); j++) {
            level = i + 1;
            is_extremum = true;
            is_repeated = false;
            is_out = false;

            // Check in case we have the same point as maxima in previous evolution levels
            for (size_t ik = 0; ik < kpts.size(); ik++) {
                if (kpts[ik].class_id == level || kpts[ik].class_id == level + 1 || kpts[ik].class_id == level - 1) {
                    dist = pow(kpts_par_[i][j].pt.x - kpts[ik].pt.x, 2) + pow(kpts_par_[i][j].pt.y - kpts[ik].pt.y, 2);

                    if (dist < evolution_[level].sigma_size*evolution_[level].sigma_size) {
                        if (kpts_par_[i][j].response > kpts[ik].response) {
                            id_repeated = ik;
                            is_repeated = true;
                        }
                        else {
                            is_extremum = false;
                        }

                        break;
                    }
                }
            }

            if (is_extremum == true) {
                // Check that the point is under the image limits for the descriptor computation
                left_x = fRound(kpts_par_[i][j].pt.x - smax*kpts_par_[i][j].size);
                right_x = fRound(kpts_par_[i][j].pt.x + smax*kpts_par_[i][j].size);
                up_y = fRound(kpts_par_[i][j].pt.y - smax*kpts_par_[i][j].size);
                down_y = fRound(kpts_par_[i][j].pt.y + smax*kpts_par_[i][j].size);

                if (left_x < 0 || right_x >= evolution_[level].Ldet.cols ||
                    up_y < 0 || down_y >= evolution_[level].Ldet.rows) {
                    is_out = true;
                }

                is_out = false;

                if (is_out == false) {
                    if (is_repeated == false) {
                        kpts.push_back(kpts_par_[i][j]);
                        npoints++;
                    }
                    else {
                        kpts[id_repeated] = kpts_par_[i][j];
                    }
                }
            }
        }
    }
}

//*************************************************************************************
//*************************************************************************************

/**
 * @brief This method is called by the thread which is responsible of finding extrema
 * at a given nonlinear scale level
 * @param level Index in the nonlinear scale space evolution
 */
void KAZEFeatures::Find_Extremum_Threading(const int& level) {

    float value = 0.0;
    bool is_extremum = false;

    for (int ix = 1; ix < img_height_ - 1; ix++) {
        for (int jx = 1; jx < img_width_ - 1; jx++) {

            is_extremum = false;
            value = *(evolution_[level].Ldet.ptr<float>(ix)+jx);

            // Filter the points with the detector threshold
            if (value > dthreshold_ && value >= DEFAULT_MIN_DETECTOR_THRESHOLD) {
                if (value >= *(evolution_[level].Ldet.ptr<float>(ix)+jx - 1)) {
                    // First check on the same scale
                    if (check_maximum_neighbourhood(evolution_[level].Ldet, 1, value, ix, jx, 1)) {
                        // Now check on the lower scale
                        if (check_maximum_neighbourhood(evolution_[level - 1].Ldet, 1, value, ix, jx, 0)) {
                            // Now check on the upper scale
                            if (check_maximum_neighbourhood(evolution_[level + 1].Ldet, 1, value, ix, jx, 0)) {
                                is_extremum = true;
                            }
                        }
                    }
                }
            }

            // Add the point of interest!!
            if (is_extremum == true) {
                KeyPoint point;
                point.pt.x = jx;
                point.pt.y = ix;
                point.response = fabs(value);
                point.size = evolution_[level].esigma;
                point.octave = evolution_[level].octave;
                point.class_id = level;

                // We use the angle field for the sublevel value
                // Then, we will replace this angle field with the main orientation
                point.angle = evolution_[level].sublevel;
                kpts_par_[level - 1].push_back(point);
            }
        }
    }
}

//*************************************************************************************
//*************************************************************************************

/**
 * @brief This method performs subpixel refinement of the detected keypoints
 * @param kpts Vector of detected keypoints
 */
void KAZEFeatures::Do_Subpixel_Refinement(std::vector<cv::KeyPoint> &kpts) {

    int step = 1;
    int x = 0, y = 0;
    float Dx = 0.0, Dy = 0.0, Ds = 0.0, dsc = 0.0;
    float Dxx = 0.0, Dyy = 0.0, Dss = 0.0, Dxy = 0.0, Dxs = 0.0, Dys = 0.0;
    Mat A = Mat::zeros(3, 3, CV_32F);
    Mat b = Mat::zeros(3, 1, CV_32F);
    Mat dst = Mat::zeros(3, 1, CV_32F);
    double t2 = 0.0, t1 = 0.0;

    t1 = cv::getTickCount();
    vector<KeyPoint> kpts_(kpts);

    for (size_t i = 0; i < kpts_.size(); i++) {

        x = kpts_[i].pt.x;
        y = kpts_[i].pt.y;

        // Compute the gradient
        Dx = (1.0 / (2.0*step))*(*(evolution_[kpts_[i].class_id].Ldet.ptr<float>(y)+x + step)
            - *(evolution_[kpts_[i].class_id].Ldet.ptr<float>(y)+x - step));
        Dy = (1.0 / (2.0*step))*(*(evolution_[kpts_[i].class_id].Ldet.ptr<float>(y + step) + x)
            - *(evolution_[kpts_[i].class_id].Ldet.ptr<float>(y - step) + x));
        Ds = 0.5*(*(evolution_[kpts_[i].class_id + 1].Ldet.ptr<float>(y)+x)
            - *(evolution_[kpts_[i].class_id - 1].Ldet.ptr<float>(y)+x));

        // Compute the Hessian
        Dxx = (1.0 / (step*step))*(*(evolution_[kpts_[i].class_id].Ldet.ptr<float>(y)+x + step)
            + *(evolution_[kpts_[i].class_id].Ldet.ptr<float>(y)+x - step)
            - 2.0*(*(evolution_[kpts_[i].class_id].Ldet.ptr<float>(y)+x)));

        Dyy = (1.0 / (step*step))*(*(evolution_[kpts_[i].class_id].Ldet.ptr<float>(y + step) + x)
            + *(evolution_[kpts_[i].class_id].Ldet.ptr<float>(y - step) + x)
            - 2.0*(*(evolution_[kpts_[i].class_id].Ldet.ptr<float>(y)+x)));

        Dss = *(evolution_[kpts_[i].class_id + 1].Ldet.ptr<float>(y)+x)
            + *(evolution_[kpts_[i].class_id - 1].Ldet.ptr<float>(y)+x)
            - 2.0*(*(evolution_[kpts_[i].class_id].Ldet.ptr<float>(y)+x));

        Dxy = (1.0 / (4.0*step))*(*(evolution_[kpts_[i].class_id].Ldet.ptr<float>(y + step) + x + step)
            + (*(evolution_[kpts_[i].class_id].Ldet.ptr<float>(y - step) + x - step)))
            - (1.0 / (4.0*step))*(*(evolution_[kpts_[i].class_id].Ldet.ptr<float>(y - step) + x + step)
            + (*(evolution_[kpts_[i].class_id].Ldet.ptr<float>(y + step) + x - step)));

        Dxs = (1.0 / (4.0*step))*(*(evolution_[kpts_[i].class_id + 1].Ldet.ptr<float>(y)+x + step)
            + (*(evolution_[kpts_[i].class_id - 1].Ldet.ptr<float>(y)+x - step)))
            - (1.0 / (4.0*step))*(*(evolution_[kpts_[i].class_id + 1].Ldet.ptr<float>(y)+x - step)
            + (*(evolution_[kpts_[i].class_id - 1].Ldet.ptr<float>(y)+x + step)));

        Dys = (1.0 / (4.0*step))*(*(evolution_[kpts_[i].class_id + 1].Ldet.ptr<float>(y + step) + x)
            + (*(evolution_[kpts_[i].class_id - 1].Ldet.ptr<float>(y - step) + x)))
            - (1.0 / (4.0*step))*(*(evolution_[kpts_[i].class_id + 1].Ldet.ptr<float>(y - step) + x)
            + (*(evolution_[kpts_[i].class_id - 1].Ldet.ptr<float>(y + step) + x)));

        // Solve the linear system
        *(A.ptr<float>(0)) = Dxx;
        *(A.ptr<float>(1) + 1) = Dyy;
        *(A.ptr<float>(2) + 2) = Dss;

        *(A.ptr<float>(0) + 1) = *(A.ptr<float>(1)) = Dxy;
        *(A.ptr<float>(0) + 2) = *(A.ptr<float>(2)) = Dxs;
        *(A.ptr<float>(1) + 2) = *(A.ptr<float>(2) + 1) = Dys;

        *(b.ptr<float>(0)) = -Dx;
        *(b.ptr<float>(1)) = -Dy;
        *(b.ptr<float>(2)) = -Ds;

        solve(A, b, dst, DECOMP_LU);

        if (fabs(*(dst.ptr<float>(0))) <= 1.0 && fabs(*(dst.ptr<float>(1))) <= 1.0 && fabs(*(dst.ptr<float>(2))) <= 1.0) {
            kpts_[i].pt.x += *(dst.ptr<float>(0));
            kpts_[i].pt.y += *(dst.ptr<float>(1));
            dsc = kpts_[i].octave + (kpts_[i].angle + *(dst.ptr<float>(2))) / ((float)(nsublevels_));

            // In OpenCV the size of a keypoint is the diameter!!
            kpts_[i].size = 2.0*soffset_*pow((float)2.0, dsc);
            kpts_[i].angle = 0.0;
        }
        // Set the points to be deleted after the for loop
        else {
            kpts_[i].response = -1;
        }
    }

    // Clear the vector of keypoints
    kpts.clear();

    for (size_t i = 0; i < kpts_.size(); i++) {
        if (kpts_[i].response != -1) {
            kpts.push_back(kpts_[i]);
        }
    }

    t2 = getTickCount();
    tsubpixel_ = 1000.0*(t2 - t1) / getTickFrequency();
}

//*************************************************************************************
//*************************************************************************************

/**
 * @brief This method performs feature suppression based on 2D distance
 * @param kpts Vector of keypoints
 * @param mdist Maximum distance in pixels
 */
void KAZEFeatures::Feature_Suppression_Distance(std::vector<cv::KeyPoint>& kpts, const float& mdist) {

    vector<KeyPoint> aux;
    vector<int> to_delete;
    float dist = 0.0, x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0;
    bool found = false;

    for (size_t i = 0; i < kpts.size(); i++) {
        x1 = kpts[i].pt.x;
        y1 = kpts[i].pt.y;

        for (size_t j = i + 1; j < kpts.size(); j++) {
            x2 = kpts[j].pt.x;
            y2 = kpts[j].pt.y;
            dist = sqrt(pow(x1 - x2, 2) + pow(y1 - y2, 2));

            if (dist < mdist) {
                if (fabs(kpts[i].response) >= fabs(kpts[j].response)) {
                    to_delete.push_back(j);
                }
                else {
                    to_delete.push_back(i);
                    break;
                }
            }
        }
    }

    for (size_t i = 0; i < kpts.size(); i++) {
        found = false;

        for (size_t j = 0; j < to_delete.size(); j++) {
            if (i == (size_t)(to_delete[j])) {
                found = true;
                break;
            }
        }

        if (found == false) {
            aux.push_back(kpts[i]);
        }
    }

    kpts.clear();
    kpts = aux;
    aux.clear();
}

//*************************************************************************************
//*************************************************************************************

/**
 * @brief This method  computes the set of descriptors through the nonlinear scale space
 * @param kpts Vector of keypoints
 * @param desc Matrix with the feature descriptors
 */
void KAZEFeatures::Feature_Description(std::vector<cv::KeyPoint> &kpts, cv::Mat &desc) {

    double t2 = 0.0, t1 = 0.0;
    t1 = getTickCount();

    // Allocate memory for the matrix of descriptors
    if (use_extended_ == true) {
        desc = Mat::zeros(kpts.size(), 128, CV_32FC1);
    }
    else {
        desc = Mat::zeros(kpts.size(), 64, CV_32FC1);
    }

    if (use_upright_ == true) {
        if (use_extended_ == false) {
            if (descriptor_mode_ == 0) {
#ifdef _OPENMP
#pragma omp parallel for
#endif
                for (size_t i = 0; i < kpts.size(); i++) {
                    kpts[i].angle = 0.0;
                    Get_SURF_Upright_Descriptor_64(kpts[i], desc.ptr<float>(i));
                }
            }
            else if (descriptor_mode_ == 1) {
#ifdef _OPENMP
#pragma omp parallel for
#endif
                for (size_t i = 0; i < kpts.size(); i++) {
                    kpts[i].angle = 0.0;
                    Get_MSURF_Upright_Descriptor_64(kpts[i], desc.ptr<float>(i));
                }
            }
            else if (descriptor_mode_ == 2) {
#ifdef _OPENMP
#pragma omp parallel for
#endif
                for (size_t i = 0; i < kpts.size(); i++) {
                    kpts[i].angle = 0.0;
                    Get_GSURF_Upright_Descriptor_64(kpts[i], desc.ptr<float>(i));
                }
            }
        }
        else
        {
            if (descriptor_mode_ == 0) {
#ifdef _OPENMP
#pragma omp parallel for
#endif
                for (size_t i = 0; i < kpts.size(); i++) {
                    kpts[i].angle = 0.0;
                    Get_SURF_Upright_Descriptor_128(kpts[i], desc.ptr<float>(i));
                }
            }
            else if (descriptor_mode_ == 1) {
#ifdef _OPENMP
#pragma omp parallel for
#endif
                for (size_t i = 0; i < kpts.size(); i++) {
                    kpts[i].angle = 0.0;
                    Get_MSURF_Upright_Descriptor_128(kpts[i], desc.ptr<float>(i));
                }
            }
            else if (descriptor_mode_ == 2) {
#ifdef _OPENMP
#pragma omp parallel for
#endif
                for (size_t i = 0; i < kpts.size(); i++) {
                    kpts[i].angle = 0.0;
                    Get_GSURF_Upright_Descriptor_128(kpts[i], desc.ptr<float>(i));
                }
            }
        }
    }
    else {
        if (use_extended_ == false) {
            if (descriptor_mode_ == 0) {
#ifdef _OPENMP
#pragma omp parallel for
#endif
                for (size_t i = 0; i < kpts.size(); i++) {
                    Compute_Main_Orientation_SURF(kpts[i]);
                    Get_SURF_Descriptor_64(kpts[i], desc.ptr<float>(i));
                }
            }
            else if (descriptor_mode_ == 1) {
#ifdef _OPENMP
#pragma omp parallel for
#endif
                for (size_t i = 0; i < kpts.size(); i++) {
                    Compute_Main_Orientation_SURF(kpts[i]);
                    Get_MSURF_Descriptor_64(kpts[i], desc.ptr<float>(i));
                }
            }
            else if (descriptor_mode_ == 2) {
#ifdef _OPENMP
#pragma omp parallel for
#endif
                for (size_t i = 0; i < kpts.size(); i++) {
                    Compute_Main_Orientation_SURF(kpts[i]);
                    Get_GSURF_Descriptor_64(kpts[i], desc.ptr<float>(i));
                }
            }
        }
        else {
            if (descriptor_mode_ == 0) {
#ifdef _OPENMP
#pragma omp parallel for
#endif
                for (size_t i = 0; i < kpts.size(); i++) {
                    Compute_Main_Orientation_SURF(kpts[i]);
                    Get_SURF_Descriptor_128(kpts[i], desc.ptr<float>(i));
                }
            }
            else if (descriptor_mode_ == 1) {
#ifdef _OPENMP
#pragma omp parallel for
#endif
                for (size_t i = 0; i < kpts.size(); i++) {
                    Compute_Main_Orientation_SURF(kpts[i]);
                    Get_MSURF_Descriptor_128(kpts[i], desc.ptr<float>(i));
                }
            }
            else if (descriptor_mode_ == 2) {
#ifdef _OPENMP
#pragma omp parallel for
#endif
                for (size_t i = 0; i < kpts.size(); i++) {
                    Compute_Main_Orientation_SURF(kpts[i]);
                    Get_GSURF_Descriptor_128(kpts[i], desc.ptr<float>(i));
                }
            }
        }
    }

    t2 = getTickCount();
    tdescriptor_ = 1000.0*(t2 - t1) / getTickFrequency();
}

//*************************************************************************************
//*************************************************************************************

/**
 * @brief This method computes the main orientation for a given keypoint
 * @param kpt Input keypoint
 * @note The orientation is computed using a similar approach as described in the
 * original SURF method. See Bay et al., Speeded Up Robust Features, ECCV 2006
 */
void KAZEFeatures::Compute_Main_Orientation_SURF(cv::KeyPoint &kpt)
{
    int ix = 0, iy = 0, idx = 0, s = 0, level = 0;
    float xf = 0.0, yf = 0.0, gweight = 0.0;
    vector<float> resX(109), resY(109), Ang(109);

    // Variables for computing the dominant direction
    float sumX = 0.0, sumY = 0.0, max = 0.0, ang1 = 0.0, ang2 = 0.0;

    // Get the information from the keypoint
    xf = kpt.pt.x;
    yf = kpt.pt.y;
    level = kpt.class_id;
    s = fRound(kpt.size / 2.0);

    // Calculate derivatives responses for points within radius of 6*scale
    for (int i = -6; i <= 6; ++i) {
        for (int j = -6; j <= 6; ++j) {
            if (i*i + j*j < 36) {
                iy = fRound(yf + j*s);
                ix = fRound(xf + i*s);

                if (iy >= 0 && iy < img_height_ && ix >= 0 && ix < img_width_) {
                    gweight = gaussian(iy - yf, ix - xf, 2.5*s);
                    resX[idx] = gweight*(*(evolution_[level].Lx.ptr<float>(iy)+ix));
                    resY[idx] = gweight*(*(evolution_[level].Ly.ptr<float>(iy)+ix));
                }
                else {
                    resX[idx] = 0.0;
                    resY[idx] = 0.0;
                }

                Ang[idx] = getAngle(resX[idx], resY[idx]);
                ++idx;
            }
        }
    }

    // Loop slides pi/3 window around feature point
    for (ang1 = 0; ang1 < 2.0*CV_PI; ang1 += 0.15f) {
        ang2 = (ang1 + CV_PI / 3.0f > 2.0*CV_PI ? ang1 - 5.0f*CV_PI / 3.0f : ang1 + CV_PI / 3.0f);
        sumX = sumY = 0.f;

        for (size_t k = 0; k < Ang.size(); ++k) {
            // Get angle from the x-axis of the sample point
            const float & ang = Ang[k];

            // Determine whether the point is within the window
            if (ang1 < ang2 && ang1 < ang && ang < ang2) {
                sumX += resX[k];
                sumY += resY[k];
            }
            else if (ang2 < ang1 &&
                ((ang > 0 && ang < ang2) || (ang > ang1 && ang < 2.0*CV_PI))) {
                sumX += resX[k];
                sumY += resY[k];
            }
        }

        // if the vector produced from this window is longer than all
        // previous vectors then this forms the new dominant direction
        if (sumX*sumX + sumY*sumY > max) {
            // store largest orientation
            max = sumX*sumX + sumY*sumY;
            kpt.angle = getAngle(sumX, sumY);
        }
    }
}

//*************************************************************************************
//*************************************************************************************

/**
 * @brief This method computes the upright descriptor (no rotation invariant)
 * of the provided keypoint
 * @param kpt Input keypoint
 * @param desc Descriptor vector
 * @note Rectangular grid of 20 s x 20 s. Descriptor Length 64. No additional
 * Gaussian weighting is performed. The descriptor is inspired from Bay et al.,
 * Speeded Up Robust Features, ECCV, 2006
 */
void KAZEFeatures::Get_SURF_Upright_Descriptor_64(const cv::KeyPoint &kpt, float *desc)
{
    float dx = 0.0, dy = 0.0, mdx = 0.0, mdy = 0.0;
    float rx = 0.0, ry = 0.0, len = 0.0, xf = 0.0, yf = 0.0, sample_x = 0.0, sample_y = 0.0;
    float fx = 0.0, fy = 0.0, res1 = 0.0, res2 = 0.0, res3 = 0.0, res4 = 0.0;
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0, sample_step = 0, pattern_size = 0, dcount = 0;
    int dsize = 0, scale = 0, level = 0;

    // Set the descriptor size and the sample and pattern sizes
    dsize = 64;
    sample_step = 5;
    pattern_size = 10;

    // Get the information from the keypoint
    yf = kpt.pt.y;
    xf = kpt.pt.x;
    level = kpt.class_id;
    scale = fRound(kpt.size / 2.0);

    // Calculate descriptor for this interest point
    for (int i = -pattern_size; i < pattern_size; i += sample_step) {
        for (int j = -pattern_size; j < pattern_size; j += sample_step) {

            dx = dy = mdx = mdy = 0.0;

            for (int k = i; k < i + sample_step; k++) {
                for (int l = j; l < j + sample_step; l++) {

                    sample_y = k*scale + yf;
                    sample_x = l*scale + xf;

                    y1 = (int)(sample_y - .5);
                    x1 = (int)(sample_x - .5);

                    checkDescriptorLimits(x1, y1, img_width_, img_height_);

                    y2 = (int)(sample_y + .5);
                    x2 = (int)(sample_x + .5);

                    checkDescriptorLimits(x2, y2, img_width_, img_height_);

                    fx = sample_x - x1;
                    fy = sample_y - y1;

                    res1 = *(evolution_[level].Lx.ptr<float>(y1)+x1);
                    res2 = *(evolution_[level].Lx.ptr<float>(y1)+x2);
                    res3 = *(evolution_[level].Lx.ptr<float>(y2)+x1);
                    res4 = *(evolution_[level].Lx.ptr<float>(y2)+x2);
                    rx = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                    res1 = *(evolution_[level].Ly.ptr<float>(y1)+x1);
                    res2 = *(evolution_[level].Ly.ptr<float>(y1)+x2);
                    res3 = *(evolution_[level].Ly.ptr<float>(y2)+x1);
                    res4 = *(evolution_[level].Ly.ptr<float>(y2)+x2);
                    ry = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                    // Sum the derivatives to the cumulative descriptor
                    dx += rx;
                    dy += ry;
                    mdx += fabs(rx);
                    mdy += fabs(ry);
                }
            }

            // Add the values to the descriptor vector
            desc[dcount++] = dx;
            desc[dcount++] = dy;
            desc[dcount++] = mdx;
            desc[dcount++] = mdy;

            // Store the current length^2 of the vector
            len += dx*dx + dy*dy + mdx*mdx + mdy*mdy;
        }
    }

    // convert to unit vector
    len = sqrt(len);

    for (int i = 0; i < dsize; i++) {
        desc[i] /= len;
    }

    if (USE_CLIPPING_NORMALIZATION == true) {
        clippingDescriptor(desc, dsize, CLIPPING_NORMALIZATION_NITER, CLIPPING_NORMALIZATION_RATIO);
    }
}

//*************************************************************************************
//*************************************************************************************

/**
 * @brief This method computes the descriptor of the provided keypoint given the
 * main orientation
 * @param kpt Input keypoint
 * @param desc Descriptor vector
 * @note Rectangular grid of 20 s x 20 s. Descriptor Length 64. No additional
 * Gaussian weighting is performed. The descriptor is inspired from Bay et al.,
 * Speeded Up Robust Features, ECCV, 2006
 */
void KAZEFeatures::Get_SURF_Descriptor_64(const cv::KeyPoint &kpt, float *desc) {

    float dx = 0.0, dy = 0.0, mdx = 0.0, mdy = 0.0;
    float rx = 0.0, ry = 0.0, rrx = 0.0, rry = 0.0, len = 0.0, xf = 0.0, yf = 0.0;
    float sample_x = 0.0, sample_y = 0.0, co = 0.0, si = 0.0, angle = 0.0;
    float fx = 0.0, fy = 0.0, res1 = 0.0, res2 = 0.0, res3 = 0.0, res4 = 0.0;
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0, sample_step = 0, pattern_size = 0, dcount = 0;
    int dsize = 0, scale = 0, level = 0;

    // Set the descriptor size and the sample and pattern sizes
    dsize = 64;
    sample_step = 5;
    pattern_size = 10;

    // Get the information from the keypoint
    yf = kpt.pt.y;
    xf = kpt.pt.x;
    scale = fRound(kpt.size / 2.0);
    angle = kpt.angle;
    level = kpt.class_id;
    co = cos(angle);
    si = sin(angle);

    // Calculate descriptor for this interest point
    for (int i = -pattern_size; i < pattern_size; i += sample_step) {
        for (int j = -pattern_size; j < pattern_size; j += sample_step) {
            dx = dy = mdx = mdy = 0.0;

            for (int k = i; k < i + sample_step; k++) {
                for (int l = j; l < j + sample_step; l++) {

                    // Get the coordinates of the sample point on the rotated axis
                    sample_y = yf + (l*scale*co + k*scale*si);
                    sample_x = xf + (-l*scale*si + k*scale*co);

                    y1 = (int)(sample_y - .5);
                    x1 = (int)(sample_x - .5);

                    checkDescriptorLimits(x1, y1, img_width_, img_height_);

                    y2 = (int)(sample_y + .5);
                    x2 = (int)(sample_x + .5);

                    checkDescriptorLimits(x2, y2, img_width_, img_height_);

                    fx = sample_x - x1;
                    fy = sample_y - y1;

                    res1 = *(evolution_[level].Lx.ptr<float>(y1)+x1);
                    res2 = *(evolution_[level].Lx.ptr<float>(y1)+x2);
                    res3 = *(evolution_[level].Lx.ptr<float>(y2)+x1);
                    res4 = *(evolution_[level].Lx.ptr<float>(y2)+x2);
                    rx = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                    res1 = *(evolution_[level].Ly.ptr<float>(y1)+x1);
                    res2 = *(evolution_[level].Ly.ptr<float>(y1)+x2);
                    res3 = *(evolution_[level].Ly.ptr<float>(y2)+x1);
                    res4 = *(evolution_[level].Ly.ptr<float>(y2)+x2);
                    ry = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                    // Get the x and y derivatives on the rotated axis
                    rry = rx*co + ry*si;
                    rrx = -rx*si + ry*co;

                    // Sum the derivatives to the cumulative descriptor
                    dx += rrx;
                    dy += rry;
                    mdx += fabs(rrx);
                    mdy += fabs(rry);
                }
            }

            // Add the values to the descriptor vector
            desc[dcount++] = dx;
            desc[dcount++] = dy;
            desc[dcount++] = mdx;
            desc[dcount++] = mdy;

            // Store the current length^2 of the vector
            len += dx*dx + dy*dy + mdx*mdx + mdy*mdy;
        }
    }

    // convert to unit vector
    len = sqrt(len);

    for (int i = 0; i < dsize; i++) {
        desc[i] /= len;
    }

    if (USE_CLIPPING_NORMALIZATION == true) {
        clippingDescriptor(desc, dsize, CLIPPING_NORMALIZATION_NITER, CLIPPING_NORMALIZATION_RATIO);
    }
}

//*************************************************************************************
//*************************************************************************************

/**
 * @brief This method computes the upright descriptor (not rotation invariant) of
 * the provided keypoint
 * @param kpt Input keypoint
 * @param desc Descriptor vector
 * @note Rectangular grid of 24 s x 24 s. Descriptor Length 64. The descriptor is inspired
 * from Agrawal et al., CenSurE: Center Surround Extremas for Realtime Feature Detection and Matching,
 * ECCV 2008
 */
void KAZEFeatures::Get_MSURF_Upright_Descriptor_64(const cv::KeyPoint &kpt, float *desc)
{
    float dx = 0.0, dy = 0.0, mdx = 0.0, mdy = 0.0, gauss_s1 = 0.0, gauss_s2 = 0.0;
    float rx = 0.0, ry = 0.0, len = 0.0, xf = 0.0, yf = 0.0, ys = 0.0, xs = 0.0;
    float sample_x = 0.0, sample_y = 0.0;
    int x1 = 0, y1 = 0, sample_step = 0, pattern_size = 0;
    int x2 = 0, y2 = 0, kx = 0, ky = 0, i = 0, j = 0, dcount = 0;
    float fx = 0.0, fy = 0.0, res1 = 0.0, res2 = 0.0, res3 = 0.0, res4 = 0.0;
    int dsize = 0, scale = 0, level = 0;

    // Subregion centers for the 4x4 gaussian weighting
    float cx = -0.5, cy = 0.5;

    // Set the descriptor size and the sample and pattern sizes
    dsize = 64;
    sample_step = 5;
    pattern_size = 12;

    // Get the information from the keypoint
    yf = kpt.pt.y;
    xf = kpt.pt.x;
    scale = fRound(kpt.size / 2.0);
    level = kpt.class_id;

    i = -8;

    // Calculate descriptor for this interest point
    // Area of size 24 s x 24 s
    while (i < pattern_size) {
        j = -8;
        i = i - 4;

        cx += 1.0;
        cy = -0.5;

        while (j < pattern_size) {

            dx = dy = mdx = mdy = 0.0;
            cy += 1.0;
            j = j - 4;

            ky = i + sample_step;
            kx = j + sample_step;

            ys = yf + (ky*scale);
            xs = xf + (kx*scale);

            for (int k = i; k < i + 9; k++) {
                for (int l = j; l < j + 9; l++) {

                    sample_y = k*scale + yf;
                    sample_x = l*scale + xf;

                    //Get the gaussian weighted x and y responses
                    gauss_s1 = gaussian(xs - sample_x, ys - sample_y, 2.5*scale);

                    y1 = (int)(sample_y - .5);
                    x1 = (int)(sample_x - .5);

                    checkDescriptorLimits(x1, y1, img_width_, img_height_);

                    y2 = (int)(sample_y + .5);
                    x2 = (int)(sample_x + .5);

                    checkDescriptorLimits(x2, y2, img_width_, img_height_);

                    fx = sample_x - x1;
                    fy = sample_y - y1;

                    res1 = *(evolution_[level].Lx.ptr<float>(y1)+x1);
                    res2 = *(evolution_[level].Lx.ptr<float>(y1)+x2);
                    res3 = *(evolution_[level].Lx.ptr<float>(y2)+x1);
                    res4 = *(evolution_[level].Lx.ptr<float>(y2)+x2);
                    rx = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                    res1 = *(evolution_[level].Ly.ptr<float>(y1)+x1);
                    res2 = *(evolution_[level].Ly.ptr<float>(y1)+x2);
                    res3 = *(evolution_[level].Ly.ptr<float>(y2)+x1);
                    res4 = *(evolution_[level].Ly.ptr<float>(y2)+x2);
                    ry = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                    rx = gauss_s1*rx;
                    ry = gauss_s1*ry;

                    // Sum the derivatives to the cumulative descriptor
                    dx += rx;
                    dy += ry;
                    mdx += fabs(rx);
                    mdy += fabs(ry);
                }
            }

            // Add the values to the descriptor vector
            gauss_s2 = gaussian(cx - 2.0f, cy - 2.0f, 1.5f);

            desc[dcount++] = dx*gauss_s2;
            desc[dcount++] = dy*gauss_s2;
            desc[dcount++] = mdx*gauss_s2;
            desc[dcount++] = mdy*gauss_s2;

            len += (dx*dx + dy*dy + mdx*mdx + mdy*mdy)*gauss_s2*gauss_s2;

            j += 9;
        }

        i += 9;
    }

    // convert to unit vector
    len = sqrt(len);

    for (int i = 0; i < dsize; i++) {
        desc[i] /= len;
    }

    if (USE_CLIPPING_NORMALIZATION == true) {
        clippingDescriptor(desc, dsize, CLIPPING_NORMALIZATION_NITER, CLIPPING_NORMALIZATION_RATIO);
    }
}

//*************************************************************************************
//*************************************************************************************

/**
 * @brief This method computes the descriptor of the provided keypoint given the
 * main orientation of the keypoint
 * @param kpt Input keypoint
 * @param desc Descriptor vector
 * @note Rectangular grid of 24 s x 24 s. Descriptor Length 64. The descriptor is inspired
 * from Agrawal et al., CenSurE: Center Surround Extremas for Realtime Feature Detection and Matching,
 * ECCV 2008
 */
void KAZEFeatures::Get_MSURF_Descriptor_64(const cv::KeyPoint &kpt, float *desc)
{
    float dx = 0.0, dy = 0.0, mdx = 0.0, mdy = 0.0, gauss_s1 = 0.0, gauss_s2 = 0.0;
    float rx = 0.0, ry = 0.0, rrx = 0.0, rry = 0.0, len = 0.0, xf = 0.0, yf = 0.0, ys = 0.0, xs = 0.0;
    float sample_x = 0.0, sample_y = 0.0, co = 0.0, si = 0.0, angle = 0.0;
    float fx = 0.0, fy = 0.0, res1 = 0.0, res2 = 0.0, res3 = 0.0, res4 = 0.0;
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0, sample_step = 0, pattern_size = 0;
    int kx = 0, ky = 0, i = 0, j = 0, dcount = 0;
    int dsize = 0, scale = 0, level = 0;

    // Subregion centers for the 4x4 gaussian weighting
    float cx = -0.5, cy = 0.5;

    // Set the descriptor size and the sample and pattern sizes
    dsize = 64;
    sample_step = 5;
    pattern_size = 12;

    // Get the information from the keypoint
    yf = kpt.pt.y;
    xf = kpt.pt.x;
    scale = fRound(kpt.size / 2.0);
    angle = kpt.angle;
    level = kpt.class_id;
    co = cos(angle);
    si = sin(angle);

    i = -8;

    // Calculate descriptor for this interest point
    // Area of size 24 s x 24 s
    while (i < pattern_size) {

        j = -8;
        i = i - 4;

        cx += 1.0;
        cy = -0.5;

        while (j < pattern_size) {

            dx = dy = mdx = mdy = 0.0;
            cy += 1.0;
            j = j - 4;

            ky = i + sample_step;
            kx = j + sample_step;

            xs = xf + (-kx*scale*si + ky*scale*co);
            ys = yf + (kx*scale*co + ky*scale*si);

            for (int k = i; k < i + 9; ++k) {
                for (int l = j; l < j + 9; ++l) {

                    // Get coords of sample point on the rotated axis
                    sample_y = yf + (l*scale*co + k*scale*si);
                    sample_x = xf + (-l*scale*si + k*scale*co);

                    // Get the gaussian weighted x and y responses
                    gauss_s1 = gaussian(xs - sample_x, ys - sample_y, 2.5*scale);
                    y1 = fRound(sample_y - .5);
                    x1 = fRound(sample_x - .5);

                    checkDescriptorLimits(x1, y1, img_width_, img_height_);

                    y2 = (int)(sample_y + .5);
                    x2 = (int)(sample_x + .5);

                    checkDescriptorLimits(x2, y2, img_width_, img_height_);

                    fx = sample_x - x1;
                    fy = sample_y - y1;

                    res1 = *(evolution_[level].Lx.ptr<float>(y1)+x1);
                    res2 = *(evolution_[level].Lx.ptr<float>(y1)+x2);
                    res3 = *(evolution_[level].Lx.ptr<float>(y2)+x1);
                    res4 = *(evolution_[level].Lx.ptr<float>(y2)+x2);
                    rx = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                    res1 = *(evolution_[level].Ly.ptr<float>(y1)+x1);
                    res2 = *(evolution_[level].Ly.ptr<float>(y1)+x2);
                    res3 = *(evolution_[level].Ly.ptr<float>(y2)+x1);
                    res4 = *(evolution_[level].Ly.ptr<float>(y2)+x2);
                    ry = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                    // Get the x and y derivatives on the rotated axis
                    rry = gauss_s1*(rx*co + ry*si);
                    rrx = gauss_s1*(-rx*si + ry*co);

                    // Sum the derivatives to the cumulative descriptor
                    dx += rrx;
                    dy += rry;
                    mdx += fabs(rrx);
                    mdy += fabs(rry);
                }
            }

            // Add the values to the descriptor vector
            gauss_s2 = gaussian(cx - 2.0f, cy - 2.0f, 1.5f);
            desc[dcount++] = dx*gauss_s2;
            desc[dcount++] = dy*gauss_s2;
            desc[dcount++] = mdx*gauss_s2;
            desc[dcount++] = mdy*gauss_s2;
            len += (dx*dx + dy*dy + mdx*mdx + mdy*mdy)*gauss_s2*gauss_s2;
            j += 9;
        }
        i += 9;
    }

    // convert to unit vector
    len = sqrt(len);

    for (int i = 0; i < dsize; i++) {
        desc[i] /= len;
    }

    if (USE_CLIPPING_NORMALIZATION == true) {
        clippingDescriptor(desc, dsize, CLIPPING_NORMALIZATION_NITER, CLIPPING_NORMALIZATION_RATIO);
    }
}

//*************************************************************************************
//*************************************************************************************

/**
 * @brief This method computes the upright G-SURF descriptor of the provided keypoint
 * given the main orientation
 * @param kpt Input keypoint
 * @param desc Descriptor vector
 * @note Rectangular grid of 20 s x 20 s. Descriptor Length 64. No additional
 * G-SURF descriptor as described in Pablo F. Alcantarilla, Luis M. Bergasa and
 * Andrew J. Davison, Gauge-SURF Descriptors, Image and Vision Computing 31(1), 2013
 */
void KAZEFeatures::Get_GSURF_Upright_Descriptor_64(const cv::KeyPoint &kpt, float *desc)
{
    float dx = 0.0, dy = 0.0, mdx = 0.0, mdy = 0.0;
    float rx = 0.0, ry = 0.0, rxx = 0.0, rxy = 0.0, ryy = 0.0, len = 0.0, xf = 0.0, yf = 0.0;
    float sample_x = 0.0, sample_y = 0.0;
    float fx = 0.0, fy = 0.0, res1 = 0.0, res2 = 0.0, res3 = 0.0, res4 = 0.0;
    float lvv = 0.0, lww = 0.0, modg = 0.0;
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0, sample_step = 0, pattern_size = 0, dcount = 0;
    int dsize = 0, scale = 0, level = 0;

    // Set the descriptor size and the sample and pattern sizes
    dsize = 64;
    sample_step = 5;
    pattern_size = 10;

    // Get the information from the keypoint
    yf = kpt.pt.y;
    xf = kpt.pt.x;
    scale = fRound(kpt.size / 2.0);
    level = kpt.class_id;

    // Calculate descriptor for this interest point
    for (int i = -pattern_size; i < pattern_size; i += sample_step) {
        for (int j = -pattern_size; j < pattern_size; j += sample_step) {

            dx = dy = mdx = mdy = 0.0;

            for (int k = i; k < i + sample_step; k++) {
                for (int l = j; l < j + sample_step; l++) {

                    // Get the coordinates of the sample point on the rotated axis
                    sample_y = yf + l*scale;
                    sample_x = xf + k*scale;

                    y1 = (int)(sample_y - .5);
                    x1 = (int)(sample_x - .5);

                    checkDescriptorLimits(x1, y1, img_width_, img_height_);

                    y2 = (int)(sample_y + .5);
                    x2 = (int)(sample_x + .5);

                    checkDescriptorLimits(x2, y2, img_width_, img_height_);

                    fx = sample_x - x1;
                    fy = sample_y - y1;

                    res1 = *(evolution_[level].Lx.ptr<float>(y1)+x1);
                    res2 = *(evolution_[level].Lx.ptr<float>(y1)+x2);
                    res3 = *(evolution_[level].Lx.ptr<float>(y2)+x1);
                    res4 = *(evolution_[level].Lx.ptr<float>(y2)+x2);
                    rx = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                    res1 = *(evolution_[level].Ly.ptr<float>(y1)+x1);
                    res2 = *(evolution_[level].Ly.ptr<float>(y1)+x2);
                    res3 = *(evolution_[level].Ly.ptr<float>(y2)+x1);
                    res4 = *(evolution_[level].Ly.ptr<float>(y2)+x2);
                    ry = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                    modg = pow(rx, 2) + pow(ry, 2);

                    if (modg != 0.0) {

                        res1 = *(evolution_[level].Lxx.ptr<float>(y1)+x1);
                        res2 = *(evolution_[level].Lxx.ptr<float>(y1)+x2);
                        res3 = *(evolution_[level].Lxx.ptr<float>(y2)+x1);
                        res4 = *(evolution_[level].Lxx.ptr<float>(y2)+x2);
                        rxx = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                        res1 = *(evolution_[level].Lxy.ptr<float>(y1)+x1);
                        res2 = *(evolution_[level].Lxy.ptr<float>(y1)+x2);
                        res3 = *(evolution_[level].Lxy.ptr<float>(y2)+x1);
                        res4 = *(evolution_[level].Lxy.ptr<float>(y2)+x2);
                        rxy = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                        res1 = *(evolution_[level].Lyy.ptr<float>(y1)+x1);
                        res2 = *(evolution_[level].Lyy.ptr<float>(y1)+x2);
                        res3 = *(evolution_[level].Lyy.ptr<float>(y2)+x1);
                        res4 = *(evolution_[level].Lyy.ptr<float>(y2)+x2);
                        ryy = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                        // Lww = (Lx^2 * Lxx + 2*Lx*Lxy*Ly + Ly^2*Lyy) / (Lx^2 + Ly^2)
                        lww = (pow(rx, 2)*rxx + 2.0*rx*rxy*ry + pow(ry, 2)*ryy) / (modg);

                        // Lvv = (-2*Lx*Lxy*Ly + Lxx*Ly^2 + Lx^2*Lyy) / (Lx^2 + Ly^2)
                        lvv = (-2.0*rx*rxy*ry + rxx*pow(ry, 2) + pow(rx, 2)*ryy) / (modg);
                    }
                    else {
                        lww = 0.0;
                        lvv = 0.0;
                    }

                    // Sum the derivatives to the cumulative descriptor
                    dx += lww;
                    dy += lvv;
                    mdx += fabs(lww);
                    mdy += fabs(lvv);
                }
            }

            // Add the values to the descriptor vector
            desc[dcount++] = dx;
            desc[dcount++] = dy;
            desc[dcount++] = mdx;
            desc[dcount++] = mdy;

            // Store the current length^2 of the vector
            len += dx*dx + dy*dy + mdx*mdx + mdy*mdy;
        }
    }

    // convert to unit vector
    len = sqrt(len);

    for (int i = 0; i < dsize; i++) {
        desc[i] /= len;
    }

    if (USE_CLIPPING_NORMALIZATION == true) {
        clippingDescriptor(desc, dsize, CLIPPING_NORMALIZATION_NITER, CLIPPING_NORMALIZATION_RATIO);
    }
}

//*************************************************************************************
//*************************************************************************************

/**
 * @brief This method computes the G-SURF descriptor of the provided keypoint given the
 * main orientation
 * @param kpt Input keypoint
 * @param desc Descriptor vector
 * @note Rectangular grid of 20 s x 20 s. Descriptor Length 64. No additional
 * G-SURF descriptor as described in Pablo F. Alcantarilla, Luis M. Bergasa and
 * Andrew J. Davison, Gauge-SURF Descriptors, Image and Vision Computing 31(1), 2013
 */
void KAZEFeatures::Get_GSURF_Descriptor_64(const cv::KeyPoint &kpt, float *desc)
{
    float dx = 0.0, dy = 0.0, mdx = 0.0, mdy = 0.0;
    float rx = 0.0, ry = 0.0, rxx = 0.0, rxy = 0.0, ryy = 0.0, len = 0.0, xf = 0.0, yf = 0.0;
    float sample_x = 0.0, sample_y = 0.0, co = 0.0, si = 0.0, angle = 0.0;
    float fx = 0.0, fy = 0.0, res1 = 0.0, res2 = 0.0, res3 = 0.0, res4 = 0.0;
    float lvv = 0.0, lww = 0.0, modg = 0.0;
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0, sample_step = 0, pattern_size = 0, dcount = 0;
    int dsize = 0, scale = 0, level = 0;

    // Set the descriptor size and the sample and pattern sizes
    dsize = 64;
    sample_step = 5;
    pattern_size = 10;

    // Get the information from the keypoint
    yf = kpt.pt.y;
    xf = kpt.pt.x;
    scale = fRound(kpt.size / 2.0);
    angle = kpt.angle;
    level = kpt.class_id;
    co = cos(angle);
    si = sin(angle);

    // Calculate descriptor for this interest point
    for (int i = -pattern_size; i < pattern_size; i += sample_step) {
        for (int j = -pattern_size; j < pattern_size; j += sample_step) {

            dx = dy = mdx = mdy = 0.0;

            for (int k = i; k < i + sample_step; k++) {
                for (int l = j; l < j + sample_step; l++) {

                    // Get the coordinates of the sample point on the rotated axis
                    sample_y = yf + (l*scale*co + k*scale*si);
                    sample_x = xf + (-l*scale*si + k*scale*co);

                    y1 = (int)(sample_y - .5);
                    x1 = (int)(sample_x - .5);

                    checkDescriptorLimits(x1, y1, img_width_, img_height_);

                    y2 = (int)(sample_y + .5);
                    x2 = (int)(sample_x + .5);

                    checkDescriptorLimits(x2, y2, img_width_, img_height_);

                    fx = sample_x - x1;
                    fy = sample_y - y1;

                    res1 = *(evolution_[level].Lx.ptr<float>(y1)+x1);
                    res2 = *(evolution_[level].Lx.ptr<float>(y1)+x2);
                    res3 = *(evolution_[level].Lx.ptr<float>(y2)+x1);
                    res4 = *(evolution_[level].Lx.ptr<float>(y2)+x2);
                    rx = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                    res1 = *(evolution_[level].Ly.ptr<float>(y1)+x1);
                    res2 = *(evolution_[level].Ly.ptr<float>(y1)+x2);
                    res3 = *(evolution_[level].Ly.ptr<float>(y2)+x1);
                    res4 = *(evolution_[level].Ly.ptr<float>(y2)+x2);
                    ry = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                    modg = pow(rx, 2) + pow(ry, 2);

                    if (modg != 0.0) {

                        res1 = *(evolution_[level].Lxx.ptr<float>(y1)+x1);
                        res2 = *(evolution_[level].Lxx.ptr<float>(y1)+x2);
                        res3 = *(evolution_[level].Lxx.ptr<float>(y2)+x1);
                        res4 = *(evolution_[level].Lxx.ptr<float>(y2)+x2);
                        rxx = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                        res1 = *(evolution_[level].Lxy.ptr<float>(y1)+x1);
                        res2 = *(evolution_[level].Lxy.ptr<float>(y1)+x2);
                        res3 = *(evolution_[level].Lxy.ptr<float>(y2)+x1);
                        res4 = *(evolution_[level].Lxy.ptr<float>(y2)+x2);
                        rxy = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                        res1 = *(evolution_[level].Lyy.ptr<float>(y1)+x1);
                        res2 = *(evolution_[level].Lyy.ptr<float>(y1)+x2);
                        res3 = *(evolution_[level].Lyy.ptr<float>(y2)+x1);
                        res4 = *(evolution_[level].Lyy.ptr<float>(y2)+x2);
                        ryy = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                        // Lww = (Lx^2 * Lxx + 2*Lx*Lxy*Ly + Ly^2*Lyy) / (Lx^2 + Ly^2)
                        lww = (pow(rx, 2)*rxx + 2.0*rx*rxy*ry + pow(ry, 2)*ryy) / (modg);

                        // Lvv = (-2*Lx*Lxy*Ly + Lxx*Ly^2 + Lx^2*Lyy) / (Lx^2 + Ly^2)
                        lvv = (-2.0*rx*rxy*ry + rxx*pow(ry, 2) + pow(rx, 2)*ryy) / (modg);
                    }
                    else {
                        lww = 0.0;
                        lvv = 0.0;
                    }

                    // Sum the derivatives to the cumulative descriptor
                    dx += lww;
                    dy += lvv;
                    mdx += fabs(lww);
                    mdy += fabs(lvv);
                }
            }

            // Add the values to the descriptor vector
            desc[dcount++] = dx;
            desc[dcount++] = dy;
            desc[dcount++] = mdx;
            desc[dcount++] = mdy;

            // Store the current length^2 of the vector
            len += dx*dx + dy*dy + mdx*mdx + mdy*mdy;
        }
    }

    // convert to unit vector
    len = sqrt(len);

    for (int i = 0; i < dsize; i++) {
        desc[i] /= len;
    }

    if (USE_CLIPPING_NORMALIZATION == true) {
        clippingDescriptor(desc, dsize, CLIPPING_NORMALIZATION_NITER, CLIPPING_NORMALIZATION_RATIO);
    }

}

//*************************************************************************************
//*************************************************************************************

/**
 * @brief This method computes the upright extended descriptor (no rotation invariant)
 * of the provided keypoint
 * @param kpt Input keypoint
 * @param desc Descriptor vector
 * @note Rectangular grid of 20 s x 20 s. Descriptor Length 128. No additional
 * Gaussian weighting is performed. The descriptor is inspired from Bay et al.,
 * Speeded Up Robust Features, ECCV, 2006
 */
void KAZEFeatures::Get_SURF_Upright_Descriptor_128(const cv::KeyPoint &kpt, float *desc)
{
    float rx = 0.0, ry = 0.0, len = 0.0, xf = 0.0, yf = 0.0, sample_x = 0.0, sample_y = 0.0;
    float fx = 0.0, fy = 0.0, res1 = 0.0, res2 = 0.0, res3 = 0.0, res4 = 0.0;
    float dxp = 0.0, dyp = 0.0, mdxp = 0.0, mdyp = 0.0;
    float dxn = 0.0, dyn = 0.0, mdxn = 0.0, mdyn = 0.0;
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0, sample_step = 0, pattern_size = 0, dcount = 0;
    int dsize = 0, scale = 0, level = 0;

    // Set the descriptor size and the sample and pattern sizes
    dsize = 128;
    sample_step = 5;
    pattern_size = 10;

    // Get the information from the keypoint
    yf = kpt.pt.y;
    xf = kpt.pt.x;
    scale = fRound(kpt.size / 2.0);
    level = kpt.class_id;

    // Calculate descriptor for this interest point
    for (int i = -pattern_size; i < pattern_size; i += sample_step) {
        for (int j = -pattern_size; j < pattern_size; j += sample_step) {

            dxp = dxn = mdxp = mdxn = 0.0;
            dyp = dyn = mdyp = mdyn = 0.0;

            for (int k = i; k < i + sample_step; k++) {
                for (int l = j; l < j + sample_step; l++) {

                    sample_y = k*scale + yf;
                    sample_x = l*scale + xf;

                    y1 = (int)(sample_y - .5);
                    x1 = (int)(sample_x - .5);

                    checkDescriptorLimits(x1, y1, img_width_, img_height_);

                    y2 = (int)(sample_y + .5);
                    x2 = (int)(sample_x + .5);

                    checkDescriptorLimits(x2, y2, img_width_, img_height_);

                    fx = sample_x - x1;
                    fy = sample_y - y1;

                    res1 = *(evolution_[level].Lx.ptr<float>(y1)+x1);
                    res2 = *(evolution_[level].Lx.ptr<float>(y1)+x2);
                    res3 = *(evolution_[level].Lx.ptr<float>(y2)+x1);
                    res4 = *(evolution_[level].Lx.ptr<float>(y2)+x2);
                    rx = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                    res1 = *(evolution_[level].Ly.ptr<float>(y1)+x1);
                    res2 = *(evolution_[level].Ly.ptr<float>(y1)+x2);
                    res3 = *(evolution_[level].Ly.ptr<float>(y2)+x1);
                    res4 = *(evolution_[level].Ly.ptr<float>(y2)+x2);
                    ry = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                    // Sum the derivatives to the cumulative descriptor
                    if (ry >= 0.0) {
                        dxp += rx;
                        mdxp += fabs(rx);
                    }
                    else {
                        dxn += rx;
                        mdxn += fabs(rx);
                    }

                    if (rx >= 0.0) {
                        dyp += ry;
                        mdyp += fabs(ry);
                    }
                    else {
                        dyn += ry;
                        mdyn += fabs(ry);
                    }
                }
            }

            // Add the values to the descriptor vector
            desc[dcount++] = dxp;
            desc[dcount++] = dxn;
            desc[dcount++] = mdxp;
            desc[dcount++] = mdxn;
            desc[dcount++] = dyp;
            desc[dcount++] = dyn;
            desc[dcount++] = mdyp;
            desc[dcount++] = mdyn;

            // Store the current length^2 of the vector
            len += dxp*dxp + dxn*dxn + mdxp*mdxp + mdxn*mdxn +
                dyp*dyp + dyn*dyn + mdyp*mdyp + mdyn*mdyn;
        }
    }

    // convert to unit vector
    len = sqrt(len);

    for (int i = 0; i < dsize; i++) {
        desc[i] /= len;
    }

    if (USE_CLIPPING_NORMALIZATION == true) {
        clippingDescriptor(desc, dsize, CLIPPING_NORMALIZATION_NITER, CLIPPING_NORMALIZATION_RATIO);
    }
}

//*************************************************************************************
//*************************************************************************************

/**
 * @brief This method computes the extended descriptor of the provided keypoint given the
 * main orientation
 * @param kpt Input keypoint
 * @param desc Descriptor vector
 * @note Rectangular grid of 20 s x 20 s. Descriptor Length 128. No additional
 * Gaussian weighting is performed. The descriptor is inspired from Bay et al.,
 * Speeded Up Robust Features, ECCV, 2006
 */
void KAZEFeatures::Get_SURF_Descriptor_128(const cv::KeyPoint &kpt, float *desc)
{
    float rx = 0.0, ry = 0.0, rrx = 0.0, rry = 0.0, len = 0.0, xf = 0.0, yf = 0.0;
    float sample_x = 0.0, sample_y = 0.0, co = 0.0, si = 0.0, angle = 0.0;
    float fx = 0.0, fy = 0.0, res1 = 0.0, res2 = 0.0, res3 = 0.0, res4 = 0.0;
    float dxp = 0.0, dyp = 0.0, mdxp = 0.0, mdyp = 0.0;
    float dxn = 0.0, dyn = 0.0, mdxn = 0.0, mdyn = 0.0;
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0, sample_step = 0, pattern_size = 0, dcount = 0;
    int dsize = 0, scale = 0, level = 0;

    // Set the descriptor size and the sample and pattern sizes
    dsize = 128;
    sample_step = 5;
    pattern_size = 10;

    // Get the information from the keypoint
    yf = kpt.pt.y;
    xf = kpt.pt.x;
    scale = fRound(kpt.size / 2.0);
    angle = kpt.angle;
    level = kpt.class_id;
    co = cos(angle);
    si = sin(angle);

    // Calculate descriptor for this interest point
    for (int i = -pattern_size; i < pattern_size; i += sample_step) {
        for (int j = -pattern_size; j < pattern_size; j += sample_step) {

            dxp = dxn = mdxp = mdxn = 0.0;
            dyp = dyn = mdyp = mdyn = 0.0;

            for (int k = i; k < i + sample_step; k++) {
                for (int l = j; l < j + sample_step; l++) {

                    // Get the coordinates of the sample point on the rotated axis
                    sample_y = yf + (l*scale*co + k*scale*si);
                    sample_x = xf + (-l*scale*si + k*scale*co);

                    y1 = (int)(sample_y - .5);
                    x1 = (int)(sample_x - .5);

                    checkDescriptorLimits(x1, y1, img_width_, img_height_);

                    y2 = (int)(sample_y + .5);
                    x2 = (int)(sample_x + .5);

                    checkDescriptorLimits(x2, y2, img_width_, img_height_);

                    fx = sample_x - x1;
                    fy = sample_y - y1;

                    res1 = *(evolution_[level].Lx.ptr<float>(y1)+x1);
                    res2 = *(evolution_[level].Lx.ptr<float>(y1)+x2);
                    res3 = *(evolution_[level].Lx.ptr<float>(y2)+x1);
                    res4 = *(evolution_[level].Lx.ptr<float>(y2)+x2);
                    rx = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                    res1 = *(evolution_[level].Ly.ptr<float>(y1)+x1);
                    res2 = *(evolution_[level].Ly.ptr<float>(y1)+x2);
                    res3 = *(evolution_[level].Ly.ptr<float>(y2)+x1);
                    res4 = *(evolution_[level].Ly.ptr<float>(y2)+x2);
                    ry = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                    // Get the x and y derivatives on the rotated axis
                    rry = rx*co + ry*si;
                    rrx = -rx*si + ry*co;

                    // Sum the derivatives to the cumulative descriptor
                    if (rry >= 0.0) {
                        dxp += rrx;
                        mdxp += fabs(rrx);
                    }
                    else {
                        dxn += rrx;
                        mdxn += fabs(rrx);
                    }

                    if (rrx >= 0.0) {
                        dyp += rry;
                        mdyp += fabs(rry);
                    }
                    else {
                        dyn += rry;
                        mdyn += fabs(rry);
                    }
                }
            }

            // Add the values to the descriptor vector
            desc[dcount++] = dxp;
            desc[dcount++] = dxn;
            desc[dcount++] = mdxp;
            desc[dcount++] = mdxn;
            desc[dcount++] = dyp;
            desc[dcount++] = dyn;
            desc[dcount++] = mdyp;
            desc[dcount++] = mdyn;

            // Store the current length^2 of the vector
            len += dxp*dxp + dxn*dxn + mdxp*mdxp + mdxn*mdxn +
                dyp*dyp + dyn*dyn + mdyp*mdyp + mdyn*mdyn;
        }
    }

    // convert to unit vector
    len = sqrt(len);

    for (int i = 0; i < dsize; i++) {
        desc[i] /= len;
    }

    if (USE_CLIPPING_NORMALIZATION == true) {
        clippingDescriptor(desc, dsize, CLIPPING_NORMALIZATION_NITER, CLIPPING_NORMALIZATION_RATIO);
    }
}

//*************************************************************************************
//*************************************************************************************

/**
 * @brief This method computes the extended upright descriptor (not rotation invariant) of
 * the provided keypoint
 * @param kpt Input keypoint
 * @param desc Descriptor vector
 * @note Rectangular grid of 24 s x 24 s. Descriptor Length 128. The descriptor is inspired
 * from Agrawal et al., CenSurE: Center Surround Extremas for Realtime Feature Detection and Matching,
 * ECCV 2008
 */
void KAZEFeatures::Get_MSURF_Upright_Descriptor_128(const cv::KeyPoint &kpt, float *desc) {

    float gauss_s1 = 0.0, gauss_s2 = 0.0;
    float rx = 0.0, ry = 0.0, len = 0.0, xf = 0.0, yf = 0.0, ys = 0.0, xs = 0.0;
    float sample_x = 0.0, sample_y = 0.0;
    int x1 = 0, y1 = 0, sample_step = 0, pattern_size = 0;
    int x2 = 0, y2 = 0, kx = 0, ky = 0, i = 0, j = 0, dcount = 0;
    float fx = 0.0, fy = 0.0, res1 = 0.0, res2 = 0.0, res3 = 0.0, res4 = 0.0;
    float dxp = 0.0, dyp = 0.0, mdxp = 0.0, mdyp = 0.0;
    float dxn = 0.0, dyn = 0.0, mdxn = 0.0, mdyn = 0.0;
    int dsize = 0, scale = 0, level = 0;

    // Subregion centers for the 4x4 gaussian weighting
    float cx = -0.5, cy = 0.5;

    // Set the descriptor size and the sample and pattern sizes
    dsize = 128;
    sample_step = 5;
    pattern_size = 12;

    // Get the information from the keypoint
    yf = kpt.pt.y;
    xf = kpt.pt.x;
    scale = fRound(kpt.size / 2.0);
    level = kpt.class_id;

    i = -8;

    // Calculate descriptor for this interest point
    // Area of size 24 s x 24 s
    while (i < pattern_size) {

        j = -8;
        i = i - 4;

        cx += 1.0;
        cy = -0.5;

        while (j < pattern_size) {

            dxp = dxn = mdxp = mdxn = 0.0;
            dyp = dyn = mdyp = mdyn = 0.0;

            cy += 1.0;
            j = j - 4;

            ky = i + sample_step;
            kx = j + sample_step;

            ys = yf + (ky*scale);
            xs = xf + (kx*scale);

            for (int k = i; k < i + 9; k++) {
                for (int l = j; l < j + 9; l++) {

                    sample_y = k*scale + yf;
                    sample_x = l*scale + xf;

                    //Get the gaussian weighted x and y responses
                    gauss_s1 = gaussian(xs - sample_x, ys - sample_y, 2.50*scale);

                    y1 = (int)(sample_y - .5);
                    x1 = (int)(sample_x - .5);

                    checkDescriptorLimits(x1, y1, img_width_, img_height_);

                    y2 = (int)(sample_y + .5);
                    x2 = (int)(sample_x + .5);

                    checkDescriptorLimits(x2, y2, img_width_, img_height_);

                    fx = sample_x - x1;
                    fy = sample_y - y1;

                    res1 = *(evolution_[level].Lx.ptr<float>(y1)+x1);
                    res2 = *(evolution_[level].Lx.ptr<float>(y1)+x2);
                    res3 = *(evolution_[level].Lx.ptr<float>(y2)+x1);
                    res4 = *(evolution_[level].Lx.ptr<float>(y2)+x2);
                    rx = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                    res1 = *(evolution_[level].Ly.ptr<float>(y1)+x1);
                    res2 = *(evolution_[level].Ly.ptr<float>(y1)+x2);
                    res3 = *(evolution_[level].Ly.ptr<float>(y2)+x1);
                    res4 = *(evolution_[level].Ly.ptr<float>(y2)+x2);
                    ry = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                    rx = gauss_s1*rx;
                    ry = gauss_s1*ry;

                    // Sum the derivatives to the cumulative descriptor
                    if (ry >= 0.0) {
                        dxp += rx;
                        mdxp += fabs(rx);
                    }
                    else {
                        dxn += rx;
                        mdxn += fabs(rx);
                    }

                    if (rx >= 0.0) {
                        dyp += ry;
                        mdyp += fabs(ry);
                    }
                    else {
                        dyn += ry;
                        mdyn += fabs(ry);
                    }
                }
            }

            // Add the values to the descriptor vector
            gauss_s2 = gaussian(cx - 2.0f, cy - 2.0f, 1.5f);

            desc[dcount++] = dxp*gauss_s2;
            desc[dcount++] = dxn*gauss_s2;
            desc[dcount++] = mdxp*gauss_s2;
            desc[dcount++] = mdxn*gauss_s2;
            desc[dcount++] = dyp*gauss_s2;
            desc[dcount++] = dyn*gauss_s2;
            desc[dcount++] = mdyp*gauss_s2;
            desc[dcount++] = mdyn*gauss_s2;

            // Store the current length^2 of the vector
            len += (dxp*dxp + dxn*dxn + mdxp*mdxp + mdxn*mdxn +
                dyp*dyp + dyn*dyn + mdyp*mdyp + mdyn*mdyn)*gauss_s2*gauss_s2;

            j += 9;
        }

        i += 9;
    }

    // convert to unit vector
    len = sqrt(len);

    for (int i = 0; i < dsize; i++) {
        desc[i] /= len;
    }

    if (USE_CLIPPING_NORMALIZATION == true) {
        clippingDescriptor(desc, dsize, CLIPPING_NORMALIZATION_NITER, CLIPPING_NORMALIZATION_RATIO);
    }
}

//*************************************************************************************
//*************************************************************************************

/**
 * @brief This method computes the extended G-SURF descriptor of the provided keypoint
 * given the main orientation of the keypoint
 * @param kpt Input keypoint
 * @param desc Descriptor vector
 * @note Rectangular grid of 24 s x 24 s. Descriptor Length 128. The descriptor is inspired
 * from Agrawal et al., CenSurE: Center Surround Extremas for Realtime Feature Detection and Matching,
 * ECCV 2008
 */
void KAZEFeatures::Get_MSURF_Descriptor_128(const cv::KeyPoint &kpt, float *desc) {

    float gauss_s1 = 0.0, gauss_s2 = 0.0;
    float rx = 0.0, ry = 0.0, rrx = 0.0, rry = 0.0, len = 0.0, xf = 0.0, yf = 0.0, ys = 0.0, xs = 0.0;
    float sample_x = 0.0, sample_y = 0.0, co = 0.0, si = 0.0, angle = 0.0;
    float fx = 0.0, fy = 0.0, res1 = 0.0, res2 = 0.0, res3 = 0.0, res4 = 0.0;
    float dxp = 0.0, dyp = 0.0, mdxp = 0.0, mdyp = 0.0;
    float dxn = 0.0, dyn = 0.0, mdxn = 0.0, mdyn = 0.0;
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0, sample_step = 0, pattern_size = 0;
    int kx = 0, ky = 0, i = 0, j = 0, dcount = 0;
    int dsize = 0, scale = 0, level = 0;

    // Subregion centers for the 4x4 gaussian weighting
    float cx = -0.5, cy = 0.5;

    // Set the descriptor size and the sample and pattern sizes
    dsize = 128;
    sample_step = 5;
    pattern_size = 12;

    // Get the information from the keypoint
    yf = kpt.pt.y;
    xf = kpt.pt.x;
    scale = fRound(kpt.size / 2.0);
    angle = kpt.angle;
    level = kpt.class_id;
    co = cos(angle);
    si = sin(angle);

    i = -8;

    // Calculate descriptor for this interest point
    // Area of size 24 s x 24 s
    while (i < pattern_size) {

        j = -8;
        i = i - 4;

        cx += 1.0;
        cy = -0.5;

        while (j < pattern_size) {

            dxp = dxn = mdxp = mdxn = 0.0;
            dyp = dyn = mdyp = mdyn = 0.0;

            cy += 1.0f;
            j = j - 4;

            ky = i + sample_step;
            kx = j + sample_step;

            xs = xf + (-kx*scale*si + ky*scale*co);
            ys = yf + (kx*scale*co + ky*scale*si);

            for (int k = i; k < i + 9; ++k) {
                for (int l = j; l < j + 9; ++l) {

                    // Get coords of sample point on the rotated axis
                    sample_y = yf + (l*scale*co + k*scale*si);
                    sample_x = xf + (-l*scale*si + k*scale*co);

                    // Get the gaussian weighted x and y responses
                    gauss_s1 = gaussian(xs - sample_x, ys - sample_y, 2.5*scale);

                    y1 = fRound(sample_y - .5);
                    x1 = fRound(sample_x - .5);

                    checkDescriptorLimits(x1, y1, img_width_, img_height_);

                    y2 = (int)(sample_y + .5);
                    x2 = (int)(sample_x + .5);

                    checkDescriptorLimits(x2, y2, img_width_, img_height_);

                    fx = sample_x - x1;
                    fy = sample_y - y1;

                    res1 = *(evolution_[level].Lx.ptr<float>(y1)+x1);
                    res2 = *(evolution_[level].Lx.ptr<float>(y1)+x2);
                    res3 = *(evolution_[level].Lx.ptr<float>(y2)+x1);
                    res4 = *(evolution_[level].Lx.ptr<float>(y2)+x2);
                    rx = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                    res1 = *(evolution_[level].Ly.ptr<float>(y1)+x1);
                    res2 = *(evolution_[level].Ly.ptr<float>(y1)+x2);
                    res3 = *(evolution_[level].Ly.ptr<float>(y2)+x1);
                    res4 = *(evolution_[level].Ly.ptr<float>(y2)+x2);
                    ry = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                    // Get the x and y derivatives on the rotated axis
                    rry = gauss_s1*(rx*co + ry*si);
                    rrx = gauss_s1*(-rx*si + ry*co);

                    // Sum the derivatives to the cumulative descriptor
                    if (rry >= 0.0) {
                        dxp += rrx;
                        mdxp += fabs(rrx);
                    }
                    else {
                        dxn += rrx;
                        mdxn += fabs(rrx);
                    }

                    if (rrx >= 0.0) {
                        dyp += rry;
                        mdyp += fabs(rry);
                    }
                    else {
                        dyn += rry;
                        mdyn += fabs(rry);
                    }
                }
            }

            // Add the values to the descriptor vector
            gauss_s2 = gaussian(cx - 2.0f, cy - 2.0f, 1.5f);

            desc[dcount++] = dxp*gauss_s2;
            desc[dcount++] = dxn*gauss_s2;
            desc[dcount++] = mdxp*gauss_s2;
            desc[dcount++] = mdxn*gauss_s2;
            desc[dcount++] = dyp*gauss_s2;
            desc[dcount++] = dyn*gauss_s2;
            desc[dcount++] = mdyp*gauss_s2;
            desc[dcount++] = mdyn*gauss_s2;

            // Store the current length^2 of the vector
            len += (dxp*dxp + dxn*dxn + mdxp*mdxp + mdxn*mdxn +
                dyp*dyp + dyn*dyn + mdyp*mdyp + mdyn*mdyn)*gauss_s2*gauss_s2;

            j += 9;
        }

        i += 9;
    }

    // convert to unit vector
    len = sqrt(len);

    for (int i = 0; i < dsize; i++) {
        desc[i] /= len;
    }

    if (USE_CLIPPING_NORMALIZATION == true) {
        clippingDescriptor(desc, dsize, CLIPPING_NORMALIZATION_NITER, CLIPPING_NORMALIZATION_RATIO);
    }
}

//*************************************************************************************
//*************************************************************************************

/**
 * @brief This method computes the G-SURF upright extended descriptor
 * (no rotation invariant) of the provided keypoint
 * @param kpt Input keypoint
 * @param desc Descriptor vector
 * @note Rectangular grid of 20 s x 20 s. Descriptor Length 128. No additional
 * G-SURF descriptor as described in Pablo F. Alcantarilla, Luis M. Bergasa and
 * Andrew J. Davison, Gauge-SURF Descriptors, Image and Vision Computing 31(1), 2013
 */
void KAZEFeatures::Get_GSURF_Upright_Descriptor_128(const cv::KeyPoint &kpt, float *desc)
{
    float len = 0.0, xf = 0.0, yf = 0.0, sample_x = 0.0, sample_y = 0.0;
    float rx = 0.0, ry = 0.0, rxx = 0.0, rxy = 0.0, ryy = 0.0, modg = 0.0;
    float fx = 0.0, fy = 0.0, res1 = 0.0, res2 = 0.0, res3 = 0.0, res4 = 0.0;
    float dxp = 0.0, dyp = 0.0, mdxp = 0.0, mdyp = 0.0;
    float dxn = 0.0, dyn = 0.0, mdxn = 0.0, mdyn = 0.0, lvv = 0.0, lww = 0.0;
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0, sample_step = 0, pattern_size = 0, dcount = 0;
    int dsize = 0, scale = 0, level = 0;

    // Set the descriptor size and the sample and pattern sizes
    dsize = 128;
    sample_step = 5;
    pattern_size = 10;

    // Get the information from the keypoint
    yf = kpt.pt.y;
    xf = kpt.pt.x;
    scale = fRound(kpt.size / 2.0);
    level = kpt.class_id;

    // Calculate descriptor for this interest point
    for (int i = -pattern_size; i < pattern_size; i += sample_step) {
        for (int j = -pattern_size; j < pattern_size; j += sample_step) {

            dxp = dxn = mdxp = mdxn = 0.0;
            dyp = dyn = mdyp = mdyn = 0.0;

            for (int k = i; k < i + sample_step; k++) {
                for (int l = j; l < j + sample_step; l++) {

                    sample_y = k*scale + yf;
                    sample_x = l*scale + xf;

                    y1 = (int)(sample_y - .5);
                    x1 = (int)(sample_x - .5);

                    checkDescriptorLimits(x1, y1, img_width_, img_height_);

                    y2 = (int)(sample_y + .5);
                    x2 = (int)(sample_x + .5);

                    checkDescriptorLimits(x2, y2, img_width_, img_height_);

                    fx = sample_x - x1;
                    fy = sample_y - y1;

                    res1 = *(evolution_[level].Lx.ptr<float>(y1)+x1);
                    res2 = *(evolution_[level].Lx.ptr<float>(y1)+x2);
                    res3 = *(evolution_[level].Lx.ptr<float>(y2)+x1);
                    res4 = *(evolution_[level].Lx.ptr<float>(y2)+x2);
                    rx = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                    res1 = *(evolution_[level].Ly.ptr<float>(y1)+x1);
                    res2 = *(evolution_[level].Ly.ptr<float>(y1)+x2);
                    res3 = *(evolution_[level].Ly.ptr<float>(y2)+x1);
                    res4 = *(evolution_[level].Ly.ptr<float>(y2)+x2);
                    ry = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                    modg = pow(rx, 2) + pow(ry, 2);

                    if (modg != 0.0) {

                        res1 = *(evolution_[level].Lxx.ptr<float>(y1)+x1);
                        res2 = *(evolution_[level].Lxx.ptr<float>(y1)+x2);
                        res3 = *(evolution_[level].Lxx.ptr<float>(y2)+x1);
                        res4 = *(evolution_[level].Lxx.ptr<float>(y2)+x2);
                        rxx = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                        res1 = *(evolution_[level].Lxy.ptr<float>(y1)+x1);
                        res2 = *(evolution_[level].Lxy.ptr<float>(y1)+x2);
                        res3 = *(evolution_[level].Lxy.ptr<float>(y2)+x1);
                        res4 = *(evolution_[level].Lxy.ptr<float>(y2)+x2);
                        rxy = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                        res1 = *(evolution_[level].Lyy.ptr<float>(y1)+x1);
                        res2 = *(evolution_[level].Lyy.ptr<float>(y1)+x2);
                        res3 = *(evolution_[level].Lyy.ptr<float>(y2)+x1);
                        res4 = *(evolution_[level].Lyy.ptr<float>(y2)+x2);
                        ryy = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                        // Lww = (Lx^2 * Lxx + 2*Lx*Lxy*Ly + Ly^2*Lyy) / (Lx^2 + Ly^2)
                        lww = (pow(rx, 2)*rxx + 2.0*rx*rxy*ry + pow(ry, 2)*ryy) / (modg);

                        // Lvv = (-2*Lx*Lxy*Ly + Lxx*Ly^2 + Lx^2*Lyy) / (Lx^2 + Ly^2)
                        lvv = (-2.0*rx*rxy*ry + rxx*pow(ry, 2) + pow(rx, 2)*ryy) / (modg);
                    }
                    else {
                        lww = 0.0;
                        lvv = 0.0;
                    }

                    // Sum the derivatives to the cumulative descriptor
                    if (lww >= 0.0) {
                        dxp += lvv;
                        mdxp += fabs(lvv);
                    }
                    else {
                        dxn += lvv;
                        mdxn += fabs(lvv);
                    }

                    if (lvv >= 0.0) {
                        dyp += lww;
                        mdyp += fabs(lww);
                    }
                    else {
                        dyn += lww;
                        mdyn += fabs(lww);
                    }
                }
            }

            // Add the values to the descriptor vector
            desc[dcount++] = dxp;
            desc[dcount++] = dxn;
            desc[dcount++] = mdxp;
            desc[dcount++] = mdxn;
            desc[dcount++] = dyp;
            desc[dcount++] = dyn;
            desc[dcount++] = mdyp;
            desc[dcount++] = mdyn;

            // Store the current length^2 of the vector
            len += dxp*dxp + dxn*dxn + mdxp*mdxp + mdxn*mdxn +
                dyp*dyp + dyn*dyn + mdyp*mdyp + mdyn*mdyn;
        }
    }

    // convert to unit vector
    len = sqrt(len);

    for (int i = 0; i < dsize; i++) {
        desc[i] /= len;
    }

    if (USE_CLIPPING_NORMALIZATION == true) {
        clippingDescriptor(desc, dsize, CLIPPING_NORMALIZATION_NITER, CLIPPING_NORMALIZATION_RATIO);
    }
}

//*************************************************************************************
//*************************************************************************************

/**
 * @brief This method computes the extended descriptor of the provided keypoint given the
 * main orientation
 * @param kpt Input keypoint
 * @param desc Descriptor vector
 * @note Rectangular grid of 20 s x 20 s. Descriptor Length 128. No additional
 * G-SURF descriptor as described in Pablo F. Alcantarilla, Luis M. Bergasa and
 * Andrew J. Davison, Gauge-SURF Descriptors, Image and Vision Computing 31(1), 2013
 */
void KAZEFeatures::Get_GSURF_Descriptor_128(const cv::KeyPoint &kpt, float *desc) {

    float len = 0.0, xf = 0.0, yf = 0.0;
    float rx = 0.0, ry = 0.0, rxx = 0.0, rxy = 0.0, ryy = 0.0;
    float sample_x = 0.0, sample_y = 0.0, co = 0.0, si = 0.0, angle = 0.0;
    float fx = 0.0, fy = 0.0, res1 = 0.0, res2 = 0.0, res3 = 0.0, res4 = 0.0;
    float dxp = 0.0, dyp = 0.0, mdxp = 0.0, mdyp = 0.0;
    float dxn = 0.0, dyn = 0.0, mdxn = 0.0, mdyn = 0.0;
    float lvv = 0.0, lww = 0.0, modg = 0.0;
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0, sample_step = 0, pattern_size = 0, dcount = 0;
    int dsize = 0, scale = 0, level = 0;

    // Set the descriptor size and the sample and pattern sizes
    dsize = 128;
    sample_step = 5;
    pattern_size = 10;

    // Get the information from the keypoint
    yf = kpt.pt.y;
    xf = kpt.pt.x;
    scale = fRound(kpt.size / 2.0);
    angle = kpt.angle;
    level = kpt.class_id;
    co = cos(angle);
    si = sin(angle);

    // Calculate descriptor for this interest point
    for (int i = -pattern_size; i < pattern_size; i += sample_step) {
        for (int j = -pattern_size; j < pattern_size; j += sample_step) {

            dxp = dxn = mdxp = mdxn = 0.0;
            dyp = dyn = mdyp = mdyn = 0.0;

            for (int k = i; k < i + sample_step; k++) {
                for (int l = j; l < j + sample_step; l++) {

                    // Get the coordinates of the sample point on the rotated axis
                    sample_y = yf + (l*scale*co + k*scale*si);
                    sample_x = xf + (-l*scale*si + k*scale*co);

                    y1 = (int)(sample_y - .5);
                    x1 = (int)(sample_x - .5);

                    checkDescriptorLimits(x1, y1, img_width_, img_height_);

                    y2 = (int)(sample_y + .5);
                    x2 = (int)(sample_x + .5);

                    checkDescriptorLimits(x2, y2, img_width_, img_height_);

                    fx = sample_x - x1;
                    fy = sample_y - y1;

                    res1 = *(evolution_[level].Lx.ptr<float>(y1)+x1);
                    res2 = *(evolution_[level].Lx.ptr<float>(y1)+x2);
                    res3 = *(evolution_[level].Lx.ptr<float>(y2)+x1);
                    res4 = *(evolution_[level].Lx.ptr<float>(y2)+x2);
                    rx = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                    res1 = *(evolution_[level].Ly.ptr<float>(y1)+x1);
                    res2 = *(evolution_[level].Ly.ptr<float>(y1)+x2);
                    res3 = *(evolution_[level].Ly.ptr<float>(y2)+x1);
                    res4 = *(evolution_[level].Ly.ptr<float>(y2)+x2);
                    ry = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                    modg = pow(rx, 2) + pow(ry, 2);

                    if (modg != 0.0) {
                        res1 = *(evolution_[level].Lxx.ptr<float>(y1)+x1);
                        res2 = *(evolution_[level].Lxx.ptr<float>(y1)+x2);
                        res3 = *(evolution_[level].Lxx.ptr<float>(y2)+x1);
                        res4 = *(evolution_[level].Lxx.ptr<float>(y2)+x2);
                        rxx = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                        res1 = *(evolution_[level].Lxy.ptr<float>(y1)+x1);
                        res2 = *(evolution_[level].Lxy.ptr<float>(y1)+x2);
                        res3 = *(evolution_[level].Lxy.ptr<float>(y2)+x1);
                        res4 = *(evolution_[level].Lxy.ptr<float>(y2)+x2);
                        rxy = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                        res1 = *(evolution_[level].Lyy.ptr<float>(y1)+x1);
                        res2 = *(evolution_[level].Lyy.ptr<float>(y1)+x2);
                        res3 = *(evolution_[level].Lyy.ptr<float>(y2)+x1);
                        res4 = *(evolution_[level].Lyy.ptr<float>(y2)+x2);
                        ryy = (1.0 - fx)*(1.0 - fy)*res1 + fx*(1.0 - fy)*res2 + (1.0 - fx)*fy*res3 + fx*fy*res4;

                        // Lww = (Lx^2 * Lxx + 2*Lx*Lxy*Ly + Ly^2*Lyy) / (Lx^2 + Ly^2)
                        lww = (pow(rx, 2)*rxx + 2.0*rx*rxy*ry + pow(ry, 2)*ryy) / (modg);

                        // Lvv = (-2*Lx*Lxy*Ly + Lxx*Ly^2 + Lx^2*Lyy) / (Lx^2 + Ly^2)
                        lvv = (-2.0*rx*rxy*ry + rxx*pow(ry, 2) + pow(rx, 2)*ryy) / (modg);
                    }
                    else {
                        lww = 0.0;
                        lvv = 0.0;
                    }

                    // Sum the derivatives to the cumulative descriptor
                    if (lww >= 0.0) {
                        dxp += lvv;
                        mdxp += fabs(lvv);
                    }
                    else {
                        dxn += lvv;
                        mdxn += fabs(lvv);
                    }

                    if (lvv >= 0.0) {
                        dyp += lww;
                        mdyp += fabs(lww);
                    }
                    else {
                        dyn += lww;
                        mdyn += fabs(lww);
                    }
                }
            }

            // Add the values to the descriptor vector
            desc[dcount++] = dxp;
            desc[dcount++] = dxn;
            desc[dcount++] = mdxp;
            desc[dcount++] = mdxn;
            desc[dcount++] = dyp;
            desc[dcount++] = dyn;
            desc[dcount++] = mdyp;
            desc[dcount++] = mdyn;

            // Store the current length^2 of the vector
            len += dxp*dxp + dxn*dxn + mdxp*mdxp + mdxn*mdxn +
                dyp*dyp + dyn*dyn + mdyp*mdyp + mdyn*mdyn;
        }
    }

    // convert to unit vector
    len = sqrt(len);

    for (int i = 0; i < dsize; i++) {
        desc[i] /= len;
    }

    if (USE_CLIPPING_NORMALIZATION == true) {
        clippingDescriptor(desc, dsize, CLIPPING_NORMALIZATION_NITER, CLIPPING_NORMALIZATION_RATIO);
    }
}

//*************************************************************************************
//*************************************************************************************

/**
 * @brief This method performs a scalar non-linear diffusion step using AOS schemes
 * @param Ld Image at a given evolution step
 * @param Ldprev Image at a previous evolution step
 * @param c Conductivity image
 * @param stepsize Stepsize for the nonlinear diffusion evolution
 * @note If c is constant, the diffusion will be linear
 * If c is a matrix of the same size as Ld, the diffusion will be nonlinear
 * The stepsize can be arbitrarilly large
 */
void KAZEFeatures::AOS_Step_Scalar(cv::Mat &Ld, const cv::Mat &Ldprev, const cv::Mat &c, const float& stepsize) {

#ifdef _OPENMP
#pragma omp sections
    {
#pragma omp section
        {
            AOS_Rows(Ldprev,c,stepsize);
        }
#pragma omp section
        {
            AOS_Columns(Ldprev,c,stepsize);
        }
    }
#else
    AOS_Rows(Ldprev, c, stepsize);
    AOS_Columns(Ldprev, c, stepsize);
#endif

    Ld = 0.5*(Lty_ + Ltx_.t());
}

//*************************************************************************************
//*************************************************************************************

/**
 * @brief This method performs performs 1D-AOS for the image rows
 * @param Ldprev Image at a previous evolution step
 * @param c Conductivity image
 * @param stepsize Stepsize for the nonlinear diffusion evolution
 */
void KAZEFeatures::AOS_Rows(const cv::Mat &Ldprev, const cv::Mat &c, const float& stepsize) {

    // Operate on rows
    for (int i = 0; i < qr_.rows; i++) {
        for (int j = 0; j < qr_.cols; j++) {
            *(qr_.ptr<float>(i)+j) = *(c.ptr<float>(i)+j) + *(c.ptr<float>(i + 1) + j);
        }
    }

    for (int j = 0; j < py_.cols; j++) {
        *(py_.ptr<float>(0) + j) = *(qr_.ptr<float>(0) + j);
    }

    for (int j = 0; j < py_.cols; j++) {
        *(py_.ptr<float>(py_.rows - 1) + j) = *(qr_.ptr<float>(qr_.rows - 1) + j);
    }

    for (int i = 1; i < py_.rows - 1; i++) {
        for (int j = 0; j < py_.cols; j++) {
            *(py_.ptr<float>(i)+j) = *(qr_.ptr<float>(i - 1) + j) + *(qr_.ptr<float>(i)+j);
        }
    }

    // a = 1 + t.*p; (p is -1*p)
    // b = -t.*q;
    ay_ = 1.0 + stepsize*py_; // p is -1*p
    by_ = -stepsize*qr_;

    // Do Thomas algorithm to solve the linear system of equations
    Thomas(ay_, by_, Ldprev, Lty_);
}

//*************************************************************************************
//*************************************************************************************

/**
 * @brief This method performs performs 1D-AOS for the image columns
 * @param Ldprev Image at a previous evolution step
 * @param c Conductivity image
 * @param stepsize Stepsize for the nonlinear diffusion evolution
 */
void KAZEFeatures::AOS_Columns(const cv::Mat &Ldprev, const cv::Mat &c, const float& stepsize) {

    // Operate on columns
    for (int j = 0; j < qc_.cols; j++) {
        for (int i = 0; i < qc_.rows; i++) {
            *(qc_.ptr<float>(i)+j) = *(c.ptr<float>(i)+j) + *(c.ptr<float>(i)+j + 1);
        }
    }

    for (int i = 0; i < px_.rows; i++) {
        *(px_.ptr<float>(i)) = *(qc_.ptr<float>(i));
    }

    for (int i = 0; i < px_.rows; i++) {
        *(px_.ptr<float>(i)+px_.cols - 1) = *(qc_.ptr<float>(i)+qc_.cols - 1);
    }

    for (int j = 1; j < px_.cols - 1; j++) {
        for (int i = 0; i < px_.rows; i++) {
            *(px_.ptr<float>(i)+j) = *(qc_.ptr<float>(i)+j - 1) + *(qc_.ptr<float>(i)+j);
        }
    }

    // a = 1 + t.*p';
    ax_ = 1.0 + stepsize*px_.t();

    // b = -t.*q';
    bx_ = -stepsize*qc_.t();

    // But take care since we need to transpose the solution!!
    Mat Ldprevt = Ldprev.t();

    // Do Thomas algorithm to solve the linear system of equations
    Thomas(ax_, bx_, Ldprevt, Ltx_);
}

//*************************************************************************************
//*************************************************************************************

/**
 * @brief This method does the Thomas algorithm for solving a tridiagonal linear system
 * @note The matrix A must be strictly diagonally dominant for a stable solution
 */
void KAZEFeatures::Thomas(const cv::Mat &a, const cv::Mat &b, const cv::Mat &Ld, cv::Mat &x) {

    // Auxiliary variables
    int n = a.rows;
    Mat m = cv::Mat::zeros(a.rows, a.cols, CV_32F);
    Mat l = cv::Mat::zeros(b.rows, b.cols, CV_32F);
    Mat y = cv::Mat::zeros(Ld.rows, Ld.cols, CV_32F);

    /** A*x = d;																		   	   */
    /**	/ a1 b1  0  0 0  ...    0 \  / x1 \ = / d1 \										   */
    /**	| c1 a2 b2  0 0  ...    0 |  | x2 | = | d2 |										   */
    /**	|  0 c2 a3 b3 0  ...    0 |  | x3 | = | d3 |										   */
    /**	|  :  :  :  : 0  ...    0 |  |  : | = |  : |										   */
    /**	|  :  :  :  : 0  cn-1  an |  | xn | = | dn |										   */

    /** 1. LU decomposition
     / L = / 1				 \		U = / m1 r1			   \
     /     | l1 1 			 |	        |    m2 r2		   |
     /     |    l2 1          |			|		m3 r3	   |
     /	  |     : : :        |			|       :  :  :	   |
     /	  \           ln-1 1 /			\				mn /	*/

    for (int j = 0; j < m.cols; j++) {
        *(m.ptr<float>(0) + j) = *(a.ptr<float>(0) + j);
    }

    for (int j = 0; j < y.cols; j++) {
        *(y.ptr<float>(0) + j) = *(Ld.ptr<float>(0) + j);
    }

    // 1. Forward substitution L*y = d for y
    for (int k = 1; k < n; k++) {
        for (int j = 0; j < l.cols; j++) {
            *(l.ptr<float>(k - 1) + j) = *(b.ptr<float>(k - 1) + j) / *(m.ptr<float>(k - 1) + j);
        }

        for (int j = 0; j < m.cols; j++) {
            *(m.ptr<float>(k)+j) = *(a.ptr<float>(k)+j) - *(l.ptr<float>(k - 1) + j)*(*(b.ptr<float>(k - 1) + j));
        }

        for (int j = 0; j < y.cols; j++) {
            *(y.ptr<float>(k)+j) = *(Ld.ptr<float>(k)+j) - *(l.ptr<float>(k - 1) + j)*(*(y.ptr<float>(k - 1) + j));
        }
    }

    // 2. Backward substitution U*x = y
    for (int j = 0; j < y.cols; j++) {
        *(x.ptr<float>(n - 1) + j) = (*(y.ptr<float>(n - 1) + j)) / (*(m.ptr<float>(n - 1) + j));
    }

    for (int i = n - 2; i >= 0; i--) {
        for (int j = 0; j < x.cols; j++) {
            *(x.ptr<float>(i)+j) = (*(y.ptr<float>(i)+j) - (*(b.ptr<float>(i)+j))*(*(x.ptr<float>(i + 1) + j))) / (*(m.ptr<float>(i)+j));
        }
    }
}

//*************************************************************************************
//*************************************************************************************

/**
 * @brief This function computes the angle from the vector given by (X Y). From 0 to 2*Pi
 */
inline float getAngle(const float& x, const float& y) {

    if (x >= 0 && y >= 0)
    {
        return atan(y / x);
    }

    if (x < 0 && y >= 0) {
        return CV_PI - atan(-y / x);
    }

    if (x < 0 && y < 0) {
        return CV_PI + atan(y / x);
    }

    if (x >= 0 && y < 0) {
        return 2.0*CV_PI - atan(-y / x);
    }

    return 0;
}

//*************************************************************************************
//*************************************************************************************

/**
 * @brief This function performs descriptor clipping
 * @param desc_ Pointer to the descriptor vector
 * @param dsize Size of the descriptor vector
 * @param iter Number of iterations
 * @param ratio Clipping ratio
 */
inline void clippingDescriptor(float *desc, const int& dsize, const int& niter, const float& ratio) {

    float cratio = ratio / sqrt(dsize);
    float len = 0.0;

    for (int i = 0; i < niter; i++) {
        len = 0.0;
        for (int j = 0; j < dsize; j++) {
            if (desc[j] > cratio) {
                desc[j] = cratio;
            }
            else if (desc[j] < -cratio) {
                desc[j] = -cratio;
            }
            len += desc[j] * desc[j];
        }

        // Normalize again
        len = sqrt(len);

        for (int j = 0; j < dsize; j++) {
            desc[j] = desc[j] / len;
        }
    }
}

//**************************************************************************************
//**************************************************************************************

/**
 * @brief This function computes the value of a 2D Gaussian function
 * @param x X Position
 * @param y Y Position
 * @param sig Standard Deviation
 */
inline float gaussian(const float& x, const float& y, const float& sig) {
    return exp(-(x*x + y*y) / (2.0f*sig*sig));
}

//**************************************************************************************
//**************************************************************************************

/**
 * @brief This function checks descriptor limits
 * @param x X Position
 * @param y Y Position
 * @param width Image width
 * @param height Image height
 */
inline void checkDescriptorLimits(int &x, int &y, const int& width, const int& height) {

    if (x < 0) {
        x = 0;
    }

    if (y < 0) {
        y = 0;
    }

    if (x > width - 1) {
        x = width - 1;
    }

    if (y > height - 1) {
        y = height - 1;
    }
}

//*************************************************************************************
//*************************************************************************************

/**
 * @brief This funtion rounds float to nearest integer
 * @param flt Input float
 * @return dst Nearest integer
 */
inline int fRound(const float& flt)
{
    return (int)(flt + 0.5f);
}
