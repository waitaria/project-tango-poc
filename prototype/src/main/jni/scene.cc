/*
 * Copyright 2014 Google Inc. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <tango-gl/conversions.h>

#include "tango-augmented-reality/scene.h"

namespace {
    // We want to represent the device properly with respect to the ground so we'll
    // add an offset in z to our origin. We'll set this offset to 1.3 meters based
    // on the average height of a human standing with a Tango device. This allows us
    // to place a grid roughly on the ground for most users.
    const glm::vec3 kHeightOffset = glm::vec3(0.0f, 0.0f, 0.0f);

    // Color of the motion tracking trajectory.
    const tango_gl::Color kTraceColor(0.22f, 0.28f, 0.67f);

    // Color of the ground grid.
    const tango_gl::Color kGridColor(0.85f, 0.85f, 0.85f);

    // Some property for the AR cube.
    const glm::quat kCubeRotation = glm::quat(0.0f, 0.0f, 1.0f, 0.0f);
    const glm::vec3 kCubePosition = glm::vec3(0.0f, 0.0f, -1.0f);
    const glm::vec3 kCubeScale = glm::vec3(0.05f, 0.05f, 0.05f);
    const tango_gl::Color kCubeColor(1.0f, 0.f, 0.f);

    inline void Yuv2Rgb(uint8_t yValue, uint8_t uValue, uint8_t vValue, uint8_t *r,
                        uint8_t *g, uint8_t *b) {
        *r = yValue + (1.370705 * (vValue - 128));
        *g = yValue - (0.698001 * (vValue - 128)) - (0.337633 * (uValue - 128));
        *b = yValue + (1.732446 * (uValue - 128));
    }
}  // namespace

namespace tango_augmented_reality {

    Scene::Scene() { }

    Scene::~Scene() { }

    void Scene::InitGLContent() {
        // Allocating render camera and drawable object.
        // All of these objects are for visualization purposes.
        yuv_drawable_ = new YUVDrawable();
        gesture_camera_ = new tango_gl::GestureCamera();
        axis_ = new tango_gl::Axis();
        frustum_ = new tango_gl::Frustum();
        trace_ = new tango_gl::Trace();
        grid_ = new tango_gl::Grid();
        cube_ = new tango_gl::Cube();
        point_cloud_drawable_ = new PointCloudDrawable();

        trace_->SetColor(kTraceColor);
        grid_->SetColor(kGridColor);
        grid_->SetPosition(-kHeightOffset);

        cube_->SetPosition(kCubePosition);
        cube_->SetScale(kCubeScale);
        cube_->SetRotation(kCubeRotation);
        cube_->SetColor(kCubeColor);

        gesture_camera_->SetCameraType(tango_gl::GestureCamera::CameraType::kThirdPerson);
    }

    void Scene::DeleteResources() {
        delete gesture_camera_;
        delete yuv_drawable_;
        delete axis_;
        delete frustum_;
        delete trace_;
        delete grid_;
        delete cube_;
        delete point_cloud_drawable_;
    }

    void Scene::SetupViewPort(int x, int y, int w, int h) {
        if (h == 0) {
            LOGE("Setup graphic height not valid");
        }
        gesture_camera_->SetAspectRatio(static_cast<float>(w) /
                                        static_cast<float>(h));
        glViewport(x, y, w, h);
    }

    void Scene::Render(const glm::mat4 &cur_pose_transformation) {
        if (!is_yuv_texture_available_) {
            return;
        }

        FillRGBTexture();

        glEnable(GL_DEPTH_TEST);

        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

        glm::vec3 position = glm::vec3(cur_pose_transformation[3][0], cur_pose_transformation[3][1],
                                       cur_pose_transformation[3][2]);

        trace_->UpdateVertexArray(position);

        if (gesture_camera_->GetCameraType() ==
            tango_gl::GestureCamera::CameraType::kFirstPerson) {
            // In first person mode, we directly control camera's motion.
            gesture_camera_->SetTransformationMatrix(cur_pose_transformation);

            // If it's first person view, we will render the video overlay in full
            // screen, so we passed identity matrix as view and projection matrix.
            glDisable(GL_DEPTH_TEST);
            yuv_drawable_->Render(glm::mat4(1.0f), glm::mat4(1.0f));
        } else {
            // In third person or top down more, we follow the camera movement.
            gesture_camera_->SetAnchorPosition(position);

            frustum_->SetTransformationMatrix(cur_pose_transformation);
            // Set the frustum scale to 4:3, this doesn't necessarily match the physical
            // camera's aspect ratio, this is just for visualization purposes.
            frustum_->SetScale(
                    glm::vec3(1.0f, camera_image_plane_ratio_, image_plane_distance_));
            frustum_->Render(ar_camera_projection_matrix_,
                             gesture_camera_->GetViewMatrix());

            axis_->SetTransformationMatrix(cur_pose_transformation);
            axis_->Render(ar_camera_projection_matrix_,
                          gesture_camera_->GetViewMatrix());

            trace_->Render(ar_camera_projection_matrix_,
                           gesture_camera_->GetViewMatrix());
            yuv_drawable_->Render(ar_camera_projection_matrix_,
                                  gesture_camera_->GetViewMatrix());
        }

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_DEPTH_TEST);
        point_cloud_drawable_->Render(gesture_camera_->GetProjectionMatrix(),
                                      gesture_camera_->GetViewMatrix(), point_cloud_transformation,
                                      vertices);
        grid_->Render(ar_camera_projection_matrix_, gesture_camera_->GetViewMatrix());
        cube_->Render(ar_camera_projection_matrix_, gesture_camera_->GetViewMatrix());
    }

    void Scene::SetCameraType(tango_gl::GestureCamera::CameraType camera_type) {
        gesture_camera_->SetCameraType(camera_type);
        if (camera_type == tango_gl::GestureCamera::CameraType::kFirstPerson) {
            yuv_drawable_->SetParent(nullptr);
            yuv_drawable_->SetScale(glm::vec3(1.0f, 1.0f, 1.0f));
            yuv_drawable_->SetPosition(glm::vec3(0.0f, 0.0f, 0.0f));
            yuv_drawable_->SetRotation(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        } else {
            yuv_drawable_->SetScale(glm::vec3(1.0f, camera_image_plane_ratio_, 1.0f));
            yuv_drawable_->SetRotation(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
            yuv_drawable_->SetPosition(glm::vec3(0.0f, 0.0f, -image_plane_distance_));
            yuv_drawable_->SetParent(axis_);
        }
    }

    void Scene::OnTouchEvent(int touch_count, tango_gl::GestureCamera::TouchEvent event, float x0,
                             float y0, float x1, float y1) {
        gesture_camera_->OnTouchEvent(touch_count, event, x0, y0, x1, y1);
    }

    void Scene::OnFrameAvailable(const TangoImageBuffer *buffer) {
        if (yuv_drawable_->GetTextureId() == 0) {
            LOGE("yuv texture id not valid");
            return;
        }

        if (buffer->format != TANGO_HAL_PIXEL_FORMAT_YCrCb_420_SP) {
            LOGE("yuv texture format is not supported by this app");
            return;
        }

        // The memory needs to be allocated after we get the first frame because we
        // need to know the size of the image.
        if (!is_yuv_texture_available_) {
            yuv_width_ = buffer->width;
            yuv_height_ = buffer->height;
            uv_buffer_offset_ = yuv_width_ * yuv_height_;
            yuv_size_ = yuv_width_ * yuv_height_ + yuv_width_ * yuv_height_ / 2;

            // Reserve and resize the buffer size for RGB and YUV data.
            yuv_buffer_.resize(yuv_size_);
            yuv_temp_buffer_.resize(yuv_size_);
            rgb_buffer_.resize(yuv_width_ * yuv_height_ * 3);


            AllocateTexture(yuv_drawable_->GetTextureId(), yuv_width_, yuv_height_);
            is_yuv_texture_available_ = true;
        }

        std::lock_guard <std::mutex> lock(yuv_buffer_mutex_);
        memcpy(&yuv_temp_buffer_[0], buffer->data, yuv_size_);
        swap_buffer_signal_ = true;
    }

    void Scene::OnXYZijAvailable(const TangoXYZij *XYZ_ij) {
        std::vector <float> points;
        for (int i = 0; i < XYZ_ij->xyz_count; ++i) {
            points.push_back(XYZ_ij->xyz[i][0] * .9);
            points.push_back(XYZ_ij->xyz[i][1] * 1.2);
            points.push_back(XYZ_ij->xyz[i][2]);
        }
        vertices = points;
    }

    void Scene::AllocateTexture(GLuint texture_id, int width, int height) {
        glBindTexture(GL_TEXTURE_2D, texture_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE,
                     rgb_buffer_.data());
    }

    void Scene::FillRGBTexture() {
        {
            std::lock_guard <std::mutex> lock(yuv_buffer_mutex_);
            if (swap_buffer_signal_) {
                std::swap(yuv_buffer_, yuv_temp_buffer_);
                swap_buffer_signal_ = false;
            }
        }

        cv::Mat rgb_frame(yuv_width_, yuv_height_, CV_8UC3);

        for (size_t i = 0; i < yuv_height_; ++i) {
            for (size_t j = 0; j < yuv_width_; ++j) {
                size_t x_index = j;
                if (j % 2 != 0) {
                    x_index = j - 1;
                }
                size_t rgb_index = (i * yuv_width_ + j) * 3;
                // The YUV texture format is NV21,
                // yuv_buffer_ buffer layout:
                //   [y0, y1, y2, ..., yn, v0, u0, v1, u1, ..., v(n/4), u(n/4)]
                Yuv2Rgb(yuv_buffer_[i * yuv_width_ + j],
                        yuv_buffer_[uv_buffer_offset_ + (i / 2) * yuv_width_ + x_index + 1],
                        yuv_buffer_[uv_buffer_offset_ + (i / 2) * yuv_width_ + x_index],
                        &rgb_buffer_[rgb_index], &rgb_buffer_[rgb_index + 1],
                        &rgb_buffer_[rgb_index + 2]);
                rgb_frame.at<cv::Vec3b>(j, i) = cv::Vec3b(rgb_buffer_[rgb_index],
                                                          rgb_buffer_[rgb_index + 1],
                                                          rgb_buffer_[rgb_index + 2]);
            }
        }

        glBindTexture(GL_TEXTURE_2D, yuv_drawable_->GetTextureId());
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, yuv_width_, yuv_height_, 0, GL_RGB,
                     GL_UNSIGNED_BYTE, rgb_buffer_.data());
    }

}  // namespace tango_augmented_reality
