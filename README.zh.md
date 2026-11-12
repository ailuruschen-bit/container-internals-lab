# 技术学习笔记

[English](README.md) | **中文** | [日本語](README.ja.md)

这里记录学习过程中真正触动我的小问题、小实验和阶段性理解。

不追求一次搭好完整的知识体系。每篇笔记只讲清一个当时想明白的问题，之后随着理解变化继续修正。

## 笔记

- 01. [环境变量到底属于谁？](notes/zh/01-environment-variables-belong-to-processes.md)
- 02. [CPU 是怎样被操作系统“抢回来”的？](notes/zh/02-how-the-kernel-takes-back-the-cpu.md)
- 03. [Linux 文件系统、Mount 与 Mount Namespace 学习心得](notes/zh/03-where-is-container-root-filesystem.md)
- 04. [Namespace 隔离视图，Cgroup 限制用量](notes/zh/04-namespace-views-and-cgroup-limits.md)
- 05. [Linux 容器网络底层原理：从 Network Namespace 到 NAT](notes/zh/05-linux-container-networking.md)
- 06. [Docker 基础实操：把 Mount 与 Network 对应到内核机制](notes/zh/06-docker-mount-and-network-practice.md)
- 07. [Docker Image 基础操作：先学会查看镜像](notes/zh/07-docker-image-basics.md)
- 08. [我为什么需要容器化？](notes/zh/08-why-containerize-applications.md)
- 09. [Docker Image 构建实操（一）：Hello World](notes/zh/09-docker-image-build-hello-world.md)
- 10. [Docker Image 构建实操（二）：定制 Nginx 首页](notes/zh/10-docker-image-build-nginx.md)
- 11. [Docker Image 构建实操（三）：缓存、ARG/ENV 与 .dockerignore](notes/zh/11-docker-image-build-cache-args-ignore.md)
- 12. [Docker Image 构建实操（四）：Java 与 Multi-stage Build](notes/zh/12-docker-image-build-java-multistage.md)
- 13. [Docker Image 实操（五）：版本替换、回滚与 Registry](notes/zh/13-docker-image-version-rollback-registry.md)
- 14. [Docker Compose 初步：从 `docker run` 到多服务配置](notes/zh/14-docker-compose-basics.md)
- 15. [Docker `USER`：让应用以非 root 身份运行](notes/zh/15-docker-user-best-practices.md)
- 16. [Docker Compose 配置实操：从 `.env` 到 Secrets](notes/zh/16-docker-compose-env-and-secrets.md)

英文和日文译文会随笔记逐步补充。

## 实验文件

笔记中使用的 Dockerfile、源代码和 Compose 文件整理在 [`labs/`](labs/) 中，并附有说明各步骤的英文注释。

## 旧内容

此前按章节组织的容器底层文章与实验，完整保存在分支 `archive/structured-container-course-2026-10-18`。
