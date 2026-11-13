/*
 * Samuel Zhao
 * 5889
 */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define MAX_POINTS 1000
int weight_matrix[MAX_POINTS][MAX_POINTS];
int shortest_path[MAX_POINTS];
int shortest_path_length = 0;
typedef struct
{
    int x;
    int y;
} Point;

/* Xlib variables */
static Display *display_ptr;
static int screen_num;
static unsigned int display_width, display_height;
static Window win;
static unsigned int win_width, win_height;
static GC gc;

/* Input points and their original-coordinate bounding box. */
static Point *points = NULL;
static int point_count = 0;
static int point_capacity = 0;
int start_index = -1;
int target_index = -1;
static double min_x, max_x, min_y, max_y;

/* Add one point to the dynamically allocated point array. */
static void add_point(int x, int y)
{
    if (point_count == point_capacity)
    {
        int new_capacity = (point_capacity == 0) ? 16 : point_capacity * 2;
        Point *new_points = realloc(points, new_capacity * sizeof(Point));

        if (new_points == NULL)
        {
            fprintf(stderr, "Error: not enough memory to store points.\n");
            free(points);
            exit(EXIT_FAILURE);
        }

        points = new_points;
        point_capacity = new_capacity;
    }

    points[point_count].x = x;
    points[point_count].y = y;
    point_count++;
}

static int read_points(const char *filename)
{
    FILE *file = fopen(filename, "r");
    char line[256];

    if (file == NULL)
    {
        perror(filename);
        return 0;
    }

    while (fgets(line, sizeof(line), file) != NULL)
    {
        int x, y;
        char extra;

        /* Ignore blank lines. Reject non-point lines rather than silently
           treating them as valid input. */
        if (sscanf(line, " P ( %d , %d ) %c", &x, &y, &extra) >= 2)
        {
            add_point(x, y);
        }
        else
        {
            int only_whitespace = 1;
            for (size_t i = 0; line[i] != '\0'; i++)
            {
                if (line[i] != ' ' && line[i] != '\t' &&
                    line[i] != '\r' && line[i] != '\n')
                {
                    only_whitespace = 0;
                    break;
                }
            }

            if (!only_whitespace)
            {
                fprintf(stderr, "Warning: skipping invalid line: %s", line);
            }
        }
    }

    if (ferror(file))
    {
        fprintf(stderr, "Error while reading %s.\n", filename);
        fclose(file);
        return 0;
    }

    fclose(file);
    return 1;
}

/* Find the input bounds and add 10 percent padding on every side. */
static void calculate_bounds(void)
{
    min_x = max_x = points[0].x;
    min_y = max_y = points[0].y;

    for (int i = 1; i < point_count; i++)
    {
        if (points[i].x < min_x)
            min_x = points[i].x;
        if (points[i].x > max_x)
            max_x = points[i].x;
        if (points[i].y < min_y)
            min_y = points[i].y;
        if (points[i].y > max_y)
            max_y = points[i].y;
    }

    double span_x = max_x - min_x;
    double span_y = max_y - min_y;

    /* If one axis has no spread, use the other axis for its padding.
       If all points coincide, use a small nonzero range. */
    double fallback_span = fmax(span_x, span_y);
    if (fallback_span < 1.0)
        fallback_span = 10.0;
    if (span_x < 1.0)
        span_x = fallback_span;
    if (span_y < 1.0)
        span_y = fallback_span;

    min_x -= span_x * 0.10;
    max_x += span_x * 0.10;
    min_y -= span_y * 0.10;
    max_y += span_y * 0.10;
}

/* Convert input coordinates to window pixel coordinates, preserving aspect
   ratio and centering the drawing inside the window. */
static void point_to_screen(Point p, int *screen_x, int *screen_y)
{
    const double margin = 20.0;
    double available_w = (win_width > 2 * margin) ? win_width - 2 * margin : 1.0;
    double available_h = (win_height > 2 * margin) ? win_height - 2 * margin : 1.0;
    double range_x = max_x - min_x;
    double range_y = max_y - min_y;
    double scale_x = available_w / range_x;
    double scale_y = available_h / range_y;
    double scale = fmin(scale_x, scale_y);

    double drawn_w = range_x * scale;
    double drawn_h = range_y * scale;
    double left = (win_width - drawn_w) / 2.0;
    double top = (win_height - drawn_h) / 2.0;

    *screen_x = (int)lround(left + (p.x - min_x) * scale);
    /* Xlib's screen Y coordinate increases downwards. */
    *screen_y = (int)lround(top + (max_y - p.y) * scale);
}

/* Redraw all points. This is called whenever the window is exposed. */
static void redraw_points(void)
{
    XClearWindow(display_ptr, win);
    if (shortest_path_length > 1)
    {
        XSetForeground(display_ptr, gc, 0x00AA00);

        for (int i = 0; i < shortest_path_length - 1; i++)
        {
            int first = shortest_path[i];
            int second = shortest_path[i + 1];

            int x1, y1, x2, y2;

            point_to_screen(points[first], &x1, &y1);
            point_to_screen(points[second], &x2, &y2);

            XDrawLine(display_ptr, win, gc, x1, y1, x2, y2);
        }
    }
    for (int i = 0; i < point_count; i++)
    {
        int x, y;
        point_to_screen(points[i], &x, &y);
        if (i == start_index)
        {
            XSetForeground(display_ptr, gc, 0xFF0000); /* Red  */
            XFillArc(display_ptr, win, gc, x - 5, y - 5, 10, 10, 0, 360 * 64);
        }
        else if (i == target_index)
        {
            XSetForeground(display_ptr, gc, 0x0000FF); /* Blue */
            XFillArc(display_ptr, win, gc, x - 5, y - 5, 10, 10, 0, 360 * 64);
        }
        else
        {
            XSetForeground(display_ptr, gc, BlackPixel(display_ptr, screen_num));
            XFillArc(display_ptr, win, gc, x - 3, y - 3, 6, 6, 0, 360 * 64);
        }
    }

    XFlush(display_ptr);
}

static int nearest_point(int click_x, int click_y)
{
    int nearest = 0;
    double best_distance = -1;

    for (int i = 0; i < point_count; i++)
    {
        int x, y;
        point_to_screen(points[i], &x, &y);

        double dx = x - click_x;
        double dy = y - click_y;
        double distance = dx * dx + dy * dy;

        if (best_distance < 0 || distance < best_distance)
        {
            best_distance = distance;
            nearest = i;
        }
    }

    return nearest;
}
void calculate_distance(Point points[], int point_count)
{
    for (int i = 0; i < point_count; i++)
    {
        for (int j = 0; j < point_count; j++)
        {
            if (i == j)
            {
                weight_matrix[i][j] = 0;
            }
            else
            {
                double dist = sqrt(pow(points[i].x - points[j].x, 2) + pow(points[i].y - points[j].y, 2));
                weight_matrix[i][j] = (int)floor(10.0 * dist);
            }
        }
    }
}
int is_reachable_bfs(int start, int target, int max_weight, int point_count)
{
    if (start == target)
        return 1;

    int visited[MAX_POINTS] = {0};
    int queue[MAX_POINTS];
    int head = 0, tail = 0;

    queue[tail++] = start;
    visited[start] = 1;

    while (head < tail)
    {
        int current = queue[head++];
        if (current == target)
            return 1;

        for (int neighbor = 0; neighbor < point_count; neighbor++)
        {
            if (!visited[neighbor] && weight_matrix[current][neighbor] <= max_weight)
            {
                visited[neighbor] = 1;
                queue[tail++] = neighbor;
            }
        }
    }
    return 0;
}
int find_bottleneck_distance(int start, int target, int point_count)
{
    int low = 0;
    int high = 0;

    // Find the maximum possible edge weight in our graph to establish upper bound
    for (int i = 0; i < point_count; i++)
    {
        for (int j = 0; j < point_count; j++)
        {
            if (weight_matrix[i][j] > high)
            {
                high = weight_matrix[i][j];
            }
        }
    }

    int result = -1;
    while (low <= high)
    {
        int mid = low + (high - low) / 2;
        if (is_reachable_bfs(start, target, mid, point_count))
        {
            result = mid;
            high = mid - 1;
        }
        else
        {
            low = mid + 1; // 'mid' is too small to link start to target, increase threshold
        }
    }
    return result;
}
void dijkstra(int start, int target, int max_weight, int point_count)
{
    int visited[MAX_POINTS] = {0};
    int previous[MAX_POINTS];
    int distance[MAX_POINTS];

    /* Initialize all distances and predecessors. */
    for (int i = 0; i < point_count; i++)
    {
        distance[i] = -1;
        previous[i] = -1;
    }

    distance[start] = 0;

    /* Find the shortest path using only allowed edges. */
    for (int count = 0; count < point_count; count++)
    {
        int current = -1;

        /* Find the unvisited point with the smallest known distance. */
        for (int i = 0; i < point_count; i++)
        {
            if (!visited[i] && distance[i] != -1 &&
                (current == -1 || distance[i] < distance[current]))
            {
                current = i;
            }
        }

        /* No more reachable points. */
        if (current == -1)
            break;

        if (current == target)
            break;

        visited[current] = 1;

        /* Update distances to neighboring points. */
        for (int neighbor = 0; neighbor < point_count; neighbor++)
        {
            int weight = weight_matrix[current][neighbor];

            if (!visited[neighbor] &&
                weight <= max_weight &&
                (distance[neighbor] == -1 ||
                 distance[current] + weight < distance[neighbor]))
            {
                distance[neighbor] = distance[current] + weight;
                previous[neighbor] = current;
            }
        }
    }

    /* Reset the stored path before reconstructing it. */
    shortest_path_length = 0;

    if (distance[target] == -1)
    {
        printf("No path found between the selected points.\n");
        return;
    }

    /* Follow predecessor links backward from the target. */
    int current = target;

    while (current != -1)
    {
        shortest_path[shortest_path_length++] = current;
        current = previous[current];
    }

    /* Reverse the path so it runs from start to target. */
    for (int i = 0; i < shortest_path_length / 2; i++)
    {
        int temp = shortest_path[i];
        shortest_path[i] = shortest_path[shortest_path_length - 1 - i];
        shortest_path[shortest_path_length - 1 - i] = temp;
    }

    printf("Shortest path total distance: %d\n", distance[target]);
    printf("Path: ");

    for (int i = 0; i < shortest_path_length; i++)
    {
        printf("%d", shortest_path[i]);

        if (i < shortest_path_length - 1)
            printf(" -> ");
    }

    printf("\n");
}
int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s points-file\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (!read_points(argv[1]))
    {
        free(points);
        return EXIT_FAILURE;
    }

    if (point_count == 0)
    {
        fprintf(stderr, "Error: no valid points were found in %s.\n", argv[1]);
        free(points);
        return EXIT_FAILURE;
    }

    calculate_bounds();
    calculate_distance(points, point_count);

    printf("Read %d points from %s\n", point_count, argv[1]);
    printf("Padded bounding box: x=[%.2f, %.2f], y=[%.2f, %.2f]\n",
           min_x, max_x, min_y, max_y);

    /* Open the connection to the X server. */
    display_ptr = XOpenDisplay(NULL);
    if (display_ptr == NULL)
    {
        fprintf(stderr, "Error: unable to open X display.\n");
        free(points);
        return EXIT_FAILURE;
    }

    screen_num = DefaultScreen(display_ptr);
    display_width = DisplayWidth(display_ptr, screen_num);
    display_height = DisplayHeight(display_ptr, screen_num);

    /* Keep the general window setup from the instructor's example. */
    win_width = display_width / 2;
    win_height = (unsigned int)(win_width / 1.7);
    if (win_width < 200)
        win_width = 200;
    if (win_height < 150)
        win_height = 150;

    win = XCreateSimpleWindow(
        display_ptr,
        RootWindow(display_ptr, screen_num),
        0, 0, win_width, win_height, 10,
        BlackPixel(display_ptr, screen_num),
        WhitePixel(display_ptr, screen_num));

    XStoreName(display_ptr, win, "Homework 1 - Points");

    XSelectInput(display_ptr, win,
                 ExposureMask | StructureNotifyMask | ButtonPressMask);

    gc = XCreateGC(display_ptr, win, 0, NULL);
    XSetForeground(display_ptr, gc, BlackPixel(display_ptr, screen_num));

    XMapWindow(display_ptr, win);
    XFlush(display_ptr);

    /* Event loop:  */
    while (1)
    {
        XEvent report;
        XNextEvent(display_ptr, &report);

        switch (report.type)
        {
        case Expose:
            if (report.xexpose.count == 0)
            {
                redraw_points();
            }
            break;

        case ConfigureNotify:
            win_width = (unsigned int)report.xconfigure.width;
            win_height = (unsigned int)report.xconfigure.height;
            break;

        case ButtonPress:
        {
            if (report.xbutton.button == Button1)
            {
                int click_x = report.xbutton.x;
                int click_y = report.xbutton.y;
                int nearest = nearest_point(click_x, click_y);

                if (nearest != -1)
                {
                    
                    if (start_index == -1)
                    {
                        
                        start_index = nearest;
                        printf("Selected Start Point [%d]: (%d, %d)\n",
                               start_index, points[start_index].x, points[start_index].y);
                    }
                    else if (target_index == -1)
                    {
                        
                        target_index = nearest;
                        printf("Selected Target Point [%d]: (%d, %d)\n",
                               target_index, points[target_index].x, points[target_index].y);
                        int bottleneck = find_bottleneck_distance(start_index, target_index, point_count);
                        
                        if (bottleneck != -1)
                        {
                            printf("Bottleneck distance between points [%d] and [%d]: %d\n", start_index, target_index, bottleneck);
                            dijkstra(start_index, target_index, bottleneck, point_count);
                        }
                        else
                        {
                            printf("No path found between points [%d] and [%d]\n", start_index, target_index);
                        }
                        fflush(stdout);
                    }
                    else
                    {
                        /* Reset if they click a 3rd time to start over */
                        start_index = nearest;
                        target_index = -1;
                        shortest_path_length = 0;
                        printf("Reset! New Start Point [%d]: (%d, %d)\n",
                               start_index, points[start_index].x, points[start_index].y);
                    }

                    /* 4. Force a visual update */
                    redraw_points();
                }
            }
        }
        break;

        default:
            break;
        }
    }

    /* The event loop currently runs until the window/application is closed. */
    XFreeGC(display_ptr, gc);
    XCloseDisplay(display_ptr);
    free(points);
    return EXIT_SUCCESS;
}
