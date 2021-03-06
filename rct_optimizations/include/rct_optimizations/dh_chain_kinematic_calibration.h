#pragma once

#include <rct_optimizations/types.h>
#include <rct_optimizations/dh_chain.h>
#include <rct_optimizations/ceres_math_utilities.h>
#include <rct_optimizations/covariance_analysis.h>

namespace rct_optimizations
{
/**
   * @brief Create a mask of parameter indices from a matrix of boolean values
   * The indices are calculated in column-wise order because Eigen stores it's values internally in column-wise order by default
   * @param mask
   * @return
   */
inline std::vector<int> createDHMask(const Eigen::Array<bool, Eigen::Dynamic, 4>& mask)
{
  std::vector<int> out;
  out.reserve(mask.size());

  const Eigen::Index rows = mask.rows();
  for (Eigen::Index row = 0; row < mask.rows(); ++row)
  {
    for (Eigen::Index col = 0; col < mask.cols(); ++col)
    {
      if (mask(row, col))
      {
        out.push_back(rows * col + row);
      }
    }
  }

  return out;
}

struct KinematicCalibrationProblem2D3D
{
  KinematicCalibrationProblem2D3D(DHChain camera_chain_, DHChain target_chain_)
    : camera_chain(std::move(camera_chain_))
    , target_chain(std::move(target_chain_))
    , camera_mount_to_camera_guess(Eigen::Isometry3d::Identity())
    , target_mount_to_target_guess(Eigen::Isometry3d::Identity())
    , camera_base_to_target_base_guess(Eigen::Isometry3d::Identity())
  {
  }

  KinObservation2D3D::Set observations;
  CameraIntrinsics intr;

  // Optimization Variables
  DHChain camera_chain;
  DHChain target_chain;
  Eigen::Isometry3d camera_mount_to_camera_guess;
  Eigen::Isometry3d target_mount_to_target_guess;
  Eigen::Isometry3d camera_base_to_target_base_guess;

  /* Create an array of masks
   * 0. Camera DH parameters (size joints x 4)
   * 1. Target DH parameters (size joints x 4)
   * 2. Camera mount to camera position (size 3)
   * 3. Camera mount to camera angle axis (size 3)
   * 4. Target mount to target position (size 3)
   * 5. Target mount to target angle axis (size 3)
   * 6. Camera base to target base position (size 3)
   * 7. Target mount to target base angle axis (size 3)
   */
  std::array<std::vector<int>, 8> mask;

  std::string label_camera_mount_to_camera = "camera_mount_to_camera";
  std::string label_target_mount_to_target = "target_mount_to_target";
  std::string label_camera_base_to_target = "camera_base_to_target";
};

struct KinematicCalibrationResult
{
  bool converged;
  double initial_cost_per_obs;
  double final_cost_per_obs;

  Eigen::Isometry3d camera_mount_to_camera;
  Eigen::Isometry3d target_mount_to_target;
  Eigen::Isometry3d camera_base_to_target_base;
  Eigen::MatrixX4d camera_chain_dh_offsets;
  Eigen::MatrixX4d target_chain_dh_offsets;

  CovarianceResult covariance;
};

class DualDHChainCost2D3D
{
  public:
  DualDHChainCost2D3D(const Eigen::Vector2d &obs,
       const Eigen::Vector3d &point_in_target,
       const CameraIntrinsics &intr,
       const DHChain &camera_chain,
       const DHChain &target_chain,
       const Eigen::VectorXd &camera_chain_joints,
       const Eigen::VectorXd &target_chain_joints)
    : obs_(obs)
    , target_pt_(point_in_target)
    , intr_(intr)
    , camera_chain_(camera_chain)
    , target_chain_(target_chain)
    , camera_chain_joints_(camera_chain_joints)
    , target_chain_joints_(target_chain_joints)
  {
  }

  template<typename T>
  Isometry3<T> createTransform(T const *const *params, const std::size_t idx) const
  {
    Eigen::Map<const Vector3<T>> t(params[idx]);
    Eigen::Map<const Vector3<T>> aa(params[idx + 1]);

    Isometry3<T> result = Isometry3<T>::Identity() * Eigen::Translation<T, 3>(t);

    T aa_norm = aa.norm();
    if (aa_norm > std::numeric_limits<T>::epsilon())
    {
      result *= Eigen::AngleAxis<T>(aa_norm, aa.normalized());
    }

    return result;
  }

  static std::vector<double *> constructParameters(Eigen::MatrixX4d &camera_chain_dh_offsets,
                                                   Eigen::MatrixX4d &target_chain_dh_offsets,
                                                   Eigen::Vector3d &camera_mount_to_camera_position,
                                                   Eigen::Vector3d &camera_mount_to_camera_angle_axis,
                                                   Eigen::Vector3d &target_mount_to_target_position,
                                                   Eigen::Vector3d &target_mount_to_target_angle_axis,
                                                   Eigen::Vector3d &camera_chain_base_to_target_chain_base_position,
                                                   Eigen::Vector3d &camera_chain_base_to_target_chain_base_angle_axis)
  {
    std::vector<double *> parameters;
    parameters.push_back(camera_chain_dh_offsets.data());
    parameters.push_back(target_chain_dh_offsets.data());
    parameters.push_back(camera_mount_to_camera_position.data());
    parameters.push_back(camera_mount_to_camera_angle_axis.data());
    parameters.push_back(target_mount_to_target_position.data());
    parameters.push_back(target_mount_to_target_angle_axis.data());
    parameters.push_back(camera_chain_base_to_target_chain_base_position.data());
    parameters.push_back(camera_chain_base_to_target_chain_base_angle_axis.data());
    return parameters;
  }

  static std::vector<std::vector<std::string>> constructParameterLabels(const std::vector<std::array<std::string, 4>>& camera_chain_labels,
                                                                        const std::vector<std::array<std::string, 4>>& target_chain_labels,
                                                                        const std::array<std::string, 3>& camera_mount_to_camera_position_labels,
                                                                        const std::array<std::string, 3>& camera_mount_to_camera_angle_axis_labels,
                                                                        const std::array<std::string, 3>& target_mount_to_target_position_labels,
                                                                        const std::array<std::string, 3>& target_mount_to_target_angle_axis_labels,
                                                                        const std::array<std::string, 3>& camera_chain_base_to_target_chain_base_position_labels,
                                                                        const std::array<std::string, 3>& camera_chain_base_to_target_chain_base_angle_axis_labels)
  {
    std::vector<std::vector<std::string>> param_labels;
    std::vector<std::string> cc_labels_concatenated;
    for (auto cc_label : camera_chain_labels)
    {
      cc_labels_concatenated.insert(cc_labels_concatenated.end(), cc_label.begin(), cc_label.end());
    }
    param_labels.push_back(cc_labels_concatenated);

    std::vector<std::string> tc_labels_concatenated;
    for (auto tc_label : target_chain_labels)
    {
      tc_labels_concatenated.insert(tc_labels_concatenated.end(), tc_label.begin(), tc_label.end());
    }
    param_labels.push_back(tc_labels_concatenated);

    param_labels.emplace_back(camera_mount_to_camera_position_labels.begin(), camera_mount_to_camera_position_labels.end());
    param_labels.emplace_back(camera_mount_to_camera_angle_axis_labels.begin(), camera_mount_to_camera_angle_axis_labels.end());
    param_labels.emplace_back(target_mount_to_target_position_labels.begin(), target_mount_to_target_position_labels.end());
    param_labels.emplace_back(target_mount_to_target_angle_axis_labels.begin(), target_mount_to_target_angle_axis_labels.end());
    param_labels.emplace_back(camera_chain_base_to_target_chain_base_position_labels.begin(), camera_chain_base_to_target_chain_base_position_labels.end());
    param_labels.emplace_back(camera_chain_base_to_target_chain_base_angle_axis_labels.begin(), camera_chain_base_to_target_chain_base_angle_axis_labels.end());
    return param_labels;
  }

  template<typename T>
  bool operator()(T const *const *parameters, T *residual) const
  {
    // Step 1: Load the data
    // The first parameter is a pointer to the DH parameter offsets of the camera kinematic chain
    Eigen::Map<const Eigen::Matrix<T, Eigen::Dynamic, 4>> camera_chain_dh_offsets(parameters[0], camera_chain_.dof(), 4);

    // The next parameter is a pointer to the DH parameter offsets of the target kinematic chain
    Eigen::Map<const Eigen::Matrix<T, Eigen::Dynamic, 4>> target_chain_dh_offsets(parameters[1], target_chain_.dof(), 4);

    // The next two parameters are pointers to the position and angle axis of the transform from the camera mount to the camera
    std::size_t cm_to_c_idx = 2;
    const Isometry3<T> camera_mount_to_camera = createTransform(parameters, cm_to_c_idx);

    // The next two parameters are pointers to the position and angle axis of the transform from the target mount to the target
    std::size_t tm_to_t_idx = cm_to_c_idx + 2;
    const Isometry3<T> target_mount_to_target = createTransform(parameters, tm_to_t_idx);

    // The next two parameters are pointers to the position and angle axis of the transform from the camera chain base to the target chain base
    std::size_t cb_to_tb_idx = tm_to_t_idx + 2;
    const Isometry3<T> camera_base_to_target_base = createTransform(parameters, cb_to_tb_idx);

    // Step 2: Transformation math
    // Build the transforms from the camera chain base out to the camera
    Isometry3<T> camera_chain_fk = camera_chain_.getFK<T>(camera_chain_joints_.cast<T>(),
                                                          camera_chain_dh_offsets);
    Isometry3<T> camera_base_to_camera = camera_chain_fk * camera_mount_to_camera;

    // Build the transforms from the camera chain base out to the target
    Isometry3<T> target_chain_fk = target_chain_.getFK<T>(target_chain_joints_.cast<T>(),
                                                          target_chain_dh_offsets);
    Isometry3<T> camera_base_to_target = camera_base_to_target_base * target_chain_fk
                                         * target_mount_to_target;

    // Now that we have two transforms in the same frame, get the target point in the camera frame
    Isometry3<T> camera_to_target = camera_base_to_camera.inverse() * camera_base_to_target;
    Vector3<T> target_in_camera = camera_to_target * target_pt_.cast<T>();

    // Project the target into the image plane
    Vector2<T> target_in_image = projectPoint(intr_, target_in_camera);

    // Step 3: Calculate the error
    residual[0] = target_in_image.x() - obs_.x();
    residual[1] = target_in_image.y() - obs_.y();

    return true;
  }

  protected:
  Eigen::Vector2d obs_;
  Eigen::Vector3d target_pt_;
  CameraIntrinsics intr_;

  const DHChain &camera_chain_;
  const DHChain &target_chain_;

  Eigen::VectorXd camera_chain_joints_;
  Eigen::VectorXd target_chain_joints_;
};

KinematicCalibrationResult optimize(const KinematicCalibrationProblem2D3D &problem);

} // namespace rct_optimizations

