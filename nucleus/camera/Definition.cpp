/*****************************************************************************
 * Alpine Terrain Renderer
 * Copyright (C) 2022 Adam Celarek
 * Copyright (C) 2023 Jakob Lindner
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************/

#include "Definition.h"

#include <cmath>

#include <QDebug>
#include <glm/gtx/transform.hpp>

#include "sherpa/geometry.h"

// camera space in opengl / webgl:
// origin:
// - x and y: the ray that goes through the centre of the monitor
// - z: the eye position
// axis directions:
// - x points to the right
// - y points upwards
// - negative z points into the scene.
//
// https://webglfundamentals.org/webgl/lessons/webgl-3d-camera.html

nucleus::camera::Definition::Definition()
    : Definition({ 1, 1, 1 }, { 0, 0, 0 })
{
}

nucleus::camera::Definition::Definition(const glm::dvec3& position, const glm::dvec3& view_at_point) // : m_position(position)
{
    m_camera_transformation = glm::inverse(glm::lookAt(position, view_at_point, { 0, 0, 1 }));
    if (std::isnan(m_camera_transformation[0][0])) {
        m_camera_transformation = glm::inverse(glm::lookAt(position, view_at_point, { 0, 1, 0 }));
    }

    set_perspective_params(75, m_viewport_size, m_near_clipping);
}

glm::dmat4 nucleus::camera::Definition::camera_matrix() const
{
    return glm::inverse(m_camera_transformation);
}

glm::dmat4 nucleus::camera::Definition::camera_space_to_world_matrix() const
{
    return m_camera_transformation;
}

glm::dmat4 nucleus::camera::Definition::projection_matrix() const
{
    return m_projection_matrix;
}

glm::dmat4 nucleus::camera::Definition::world_view_projection_matrix() const
{
    return m_projection_matrix * camera_matrix();
}

glm::mat4 nucleus::camera::Definition::local_view_matrix() const
{
    return camera_matrix() * glm::translate(this->position());
}

glm::mat4 nucleus::camera::Definition::local_view_projection_matrix(const glm::dvec3& origin_offset) const
{
    return glm::mat4(m_projection_matrix * camera_matrix() * glm::translate(origin_offset));
}

glm::dvec3 nucleus::camera::Definition::position() const
{
    return glm::dvec3(m_camera_transformation[3]);
}

glm::dvec3 nucleus::camera::Definition::x_axis() const
{
    return glm::dvec3(m_camera_transformation[0]);
}

glm::dvec3 nucleus::camera::Definition::y_axis() const
{
    return glm::dvec3(m_camera_transformation[1]);
}

glm::dvec3 nucleus::camera::Definition::z_axis() const
{
    return glm::dvec3(m_camera_transformation[2]);
}

glm::dvec3 nucleus::camera::Definition::ray_direction(const glm::dvec2& normalised_device_coordinates) const
{
    const auto inverse_projection_matrix = glm::inverse(projection_matrix());
    const auto inverse_view_matrix = m_camera_transformation;
    const auto unprojected = inverse_projection_matrix * glm::dvec4(normalised_device_coordinates.x, normalised_device_coordinates.y, 1, 1);
    const auto normalised_unprojected = unprojected / unprojected.w;
    return glm::normalize(glm::dvec3(inverse_view_matrix * normalised_unprojected) - position());
}

nucleus::camera::Frustum nucleus::camera::Definition::frustum() const
{
    nucleus::camera::Frustum frustum;
    // front and back
    const auto p0 = position() + -z_axis() * double(m_near_clipping);
    frustum.clipping_planes[0] = {.normal = -z_axis(), .distance = -dot(-z_axis(), p0)};
    const auto p1 = position() + -z_axis() * double(m_far_clipping);
    frustum.clipping_planes[1] = {.normal = z_axis(), .distance = -dot(z_axis(), p1)};

    constexpr auto tl = glm::dvec2{-1, 1};
    constexpr auto bl = glm::dvec2{-1, -1};
    constexpr auto br = glm::dvec2{1, -1};
    constexpr auto tr = glm::dvec2{1, 1};

    const auto ray_tl = ray_direction(tl);
    const auto ray_bl = ray_direction(bl);
    const auto ray_br = ray_direction(br);
    const auto ray_tr = ray_direction(tr);


    const auto clippingPane = [this](const glm::dvec3& v_a, const glm::dvec3& v_b) {
        const auto normal = glm::normalize(cross(v_a, v_b));
        const auto distance = -dot(normal, position());
        return geometry::Plane<double>{normal, distance};
    };

    // top and down
    frustum.clipping_planes[2] = clippingPane(ray_tl, ray_tr);
    frustum.clipping_planes[3] = clippingPane(ray_br, ray_bl);

    // left and right
    frustum.clipping_planes[4] = clippingPane(ray_bl, ray_tl);
    frustum.clipping_planes[5] = clippingPane(ray_tr, ray_br);

    // near corners
    frustum.corners[0] = geometry::intersection(geometry::Line<3, double>{position(), ray_tl}, frustum.clipping_planes[0]).value();
    frustum.corners[1] = geometry::intersection(geometry::Line<3, double>{position(), ray_bl}, frustum.clipping_planes[0]).value();
    frustum.corners[2] = geometry::intersection(geometry::Line<3, double>{position(), ray_br}, frustum.clipping_planes[0]).value();
    frustum.corners[3] = geometry::intersection(geometry::Line<3, double>{position(), ray_tr}, frustum.clipping_planes[0]).value();

    // far corners
    frustum.corners[4] = geometry::intersection(geometry::Line<3, double>{position(), ray_tl}, frustum.clipping_planes[1]).value();
    frustum.corners[5] = geometry::intersection(geometry::Line<3, double>{position(), ray_bl}, frustum.clipping_planes[1]).value();
    frustum.corners[6] = geometry::intersection(geometry::Line<3, double>{position(), ray_br}, frustum.clipping_planes[1]).value();
    frustum.corners[7] = geometry::intersection(geometry::Line<3, double>{position(), ray_tr}, frustum.clipping_planes[1]).value();

    return frustum;
}

std::array<geometry::Plane<double>, 6> nucleus::camera::Definition::clipping_planes() const
{
    return frustum().clipping_planes;
}

std::vector<geometry::Plane<double>> nucleus::camera::Definition::four_clipping_planes() const
{
    const auto clippingPane = [this](const glm::dvec2& a, const glm::dvec2& b) {
        const auto v_a = ray_direction(a);
        const auto v_b = ray_direction(b);
        const auto normal = glm::normalize(cross(v_a, v_b));
        const auto distance = -dot(normal, position());
        return geometry::Plane<double> { normal, distance };
    };
    std::vector<geometry::Plane<double>> clipping_panes;
    clipping_panes.reserve(4);

    // top and down
    clipping_panes.push_back(clippingPane({ -1, 1 }, { 1, 1 }));
    clipping_panes.push_back(clippingPane({ 1, -1 }, { -1, -1 }));

    // left and right
    clipping_panes.push_back(clippingPane({ -1, -1 }, { -1, 1 }));
    clipping_panes.push_back(clippingPane({ 1, 1 }, { 1, -1 }));
    return clipping_panes;
}

// for reverse z: https://nlguillemot.wordpress.com/2016/12/07/reversed-z-in-opengl/
glm::mat4 MakeInfReversedZProjRH(float fovY_radians, float aspectWbyH, float zNear)
{
    float f = 1.0f / tan(fovY_radians / 2.0f);
    return glm::mat4(
        f / aspectWbyH, 0.0f,  0.0f,  0.0f,
        0.0f,    f,  0.0f,  0.0f,
        0.0f, 0.0f,  0.0f, -1.0f,
        0.0f, 0.0f, zNear,  0.0f);
}

void nucleus::camera::Definition::set_perspective_params(float fov_degrees, const glm::uvec2& viewport_size, float near_plane)
{
    m_distance_scaling_factor = 1.f / std::tan(0.5f * fov_degrees * 3.1415926535897932384626433f / 180);
    m_near_clipping = near_plane;
    m_far_clipping = near_plane * 1'000'000;
    m_far_clipping = std::min(m_far_clipping, 1'000'000'000.f); // will be obscured by atmosphere anyways + depth based atmosphere will have numerical issues (show background atmosphere)
    m_viewport_size = viewport_size;
    m_field_of_view = fov_degrees;
    m_projection_matrix = glm::perspective(
        glm::radians(double(fov_degrees)),
        double(viewport_size.x) / double(viewport_size.y),
        double(m_near_clipping),
        double(m_far_clipping));
    m_projection_matrix = MakeInfReversedZProjRH(glm::radians(double(fov_degrees)), double(viewport_size.x) / double(viewport_size.y), m_near_clipping);

}

void nucleus::camera::Definition::set_near_plane(float near_plane)
{
    set_perspective_params(m_field_of_view, m_viewport_size, near_plane);
}

float nucleus::camera::Definition::near_plane() const
{
    return m_near_clipping;
}

void nucleus::camera::Definition::pan(const glm::dvec2& v)
{
    const auto x_dir = x_axis();
    const auto y_dir = glm::cross(x_dir, glm::dvec3(0, 0, 1));
    m_camera_transformation = glm::translate(-1.0 * (v.x * x_dir + v.y * y_dir)) * m_camera_transformation;
}

void nucleus::camera::Definition::move(const glm::dvec3& v)
{
    m_camera_transformation = glm::translate(v) * m_camera_transformation;
}

void nucleus::camera::Definition::orbit(const glm::dvec3& centre, const glm::dvec2& degrees)
{
    move(-centre);
    const auto rotation_x_axis = glm::rotate(glm::radians(degrees.y), x_axis());
    const auto rotation_z_axis = glm::rotate(glm::radians(degrees.x), glm::dvec3(0, 0, 1));
    const auto rotation = rotation_z_axis * rotation_x_axis;
    m_camera_transformation = rotation * m_camera_transformation;
    move(centre);
}

void nucleus::camera::Definition::orbit(const glm::vec2& degrees)
{
    orbit(operation_centre(), degrees);
}

void nucleus::camera::Definition::orbit_clamped(const glm::dvec3& centre, const glm::dvec2& degrees)
{
    auto degFromUp = glm::degrees(glm::acos(glm::dot(z_axis(), glm::dvec3(0, 0, 1))));
    auto degY = degrees.y;
    if (degFromUp + degY > 179.0) {
        degY = 179.0 - degFromUp;
    }
    else if (degFromUp + degY < 1.0) {
        degY = 1.0 - degFromUp;
    }
    orbit(centre, glm::vec2(degrees.x, degY));
}

void nucleus::camera::Definition::zoom(double v)
{
    move(z_axis() * v);
}

const glm::uvec2& nucleus::camera::Definition::viewport_size() const
{
    return m_viewport_size;
}

glm::dvec2 nucleus::camera::Definition::to_ndc(const glm::dvec2& screen_space_coordinates) const
{
    // https://doc.qt.io/qt-6/coordsys.html
    // https://registry.khronos.org/OpenGL-Refpages/gl4/html/glViewport.xhtml
    return ((screen_space_coordinates / glm::dvec2(viewport_size())) * 2.0 - 1.0) * glm::dvec2 { 1.0, -1.0 };
}

float nucleus::camera::Definition::to_screen_space(float world_space_size, float world_space_distance) const
{
    return m_viewport_size.y * 0.5f * world_space_size * m_distance_scaling_factor / world_space_distance;
}

glm::dvec3 nucleus::camera::Definition::operation_centre() const
{
    // a ray going through the middle pixel, intersecting with the z == 0 pane
    const auto origin = position();
    const auto direction = -z_axis();
    const auto t = -origin.z / direction.z;
    return origin + t * direction;
}

namespace nucleus::camera {

float Definition::field_of_view() const
{
    return m_field_of_view;
}

void Definition::set_field_of_view(float new_field_of_view_degrees)
{
    set_perspective_params(new_field_of_view_degrees, m_viewport_size, m_near_clipping);
}

bool Definition::operator==(const Definition& other) const
{
    return m_camera_transformation == other.m_camera_transformation && m_projection_matrix == other.m_projection_matrix && m_viewport_size == other.m_viewport_size;
}

void Definition::set_viewport_size(const glm::uvec2& new_viewport_size)
{
    set_perspective_params(m_field_of_view, new_viewport_size, m_near_clipping);
}

}
