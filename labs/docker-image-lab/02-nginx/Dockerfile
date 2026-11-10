# Custom Nginx image: only the home page is replaced.
# ENTRYPOINT, CMD, and EXPOSE are inherited from nginx:alpine,
# so this Dockerfile does not need to declare them again.
FROM nginx:alpine

# Written to a new layer at the same path; the default page in the
# base image's read-only layer is hidden, not modified.
COPY index.html /usr/share/nginx/html/index.html
