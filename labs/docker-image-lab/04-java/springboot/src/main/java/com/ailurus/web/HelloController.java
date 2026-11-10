package com.ailurus.web;

import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RestController;

/**
 * Simple endpoint for checking which image version is running.
 * Bump the version string when testing image replacement and rollback.
 */
@RestController
public class HelloController {

    @GetMapping("/hello")
    public String hello() {
        return "Hello from Spring Boot in Docker! (v2)";
    }
}
