#include <graphene-gobject.h>
#include <stdio.h>

int main(void) {
    graphene_vec3_t vector;
    graphene_point3d_t translation;
    graphene_matrix_t matrix;
    graphene_point3d_t point;
    graphene_point3d_t transformed;
    graphene_rect_t bounds = GRAPHENE_RECT_INIT(0.0f, 0.0f, 20.0f, 12.0f);
    graphene_point_t inside = GRAPHENE_POINT_INIT(7.0f, 5.0f);
    graphene_point_t outside = GRAPHENE_POINT_INIT(24.0f, 5.0f);

    graphene_vec3_init(&vector, 4.0f, -2.0f, 3.0f);
    graphene_point3d_init(&translation, 4.0f, -2.0f, 3.0f);
    graphene_matrix_init_translate(&matrix, &translation);
    graphene_point3d_init(&point, 1.0f, 2.0f, 3.0f);
    graphene_matrix_transform_point3d(&matrix, &point, &transformed);

    if (transformed.x != 5.0f || transformed.y != 0.0f ||
        transformed.z != 6.0f ||
        !graphene_rect_contains_point(&bounds, &inside) ||
        graphene_rect_contains_point(&bounds, &outside) ||
        graphene_vec3_dot(&vector, &vector) != 29.0f) {
        puts("Graphene vector/matrix/rectangle operations failed");
        return 1;
    }

    if (!G_TYPE_IS_BOXED(graphene_matrix_get_type()) ||
        !G_TYPE_IS_BOXED(graphene_rect_get_type())) {
        puts("Graphene GObject boxed type registration failed");
        return 2;
    }

    puts("Upstream Graphene 1.10.8 vector, matrix, geometry, and GObject APIs loaded");
    puts("upstream-graphene-smoke-ok");
    return 0;
}
