package com.ailurus.web;

import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.web.bind.annotation.*;

import java.util.List;
import java.util.Map;

/**
 * Writes and reads rows in PostgreSQL, used to verify that data stored
 * in the Compose volume survives container recreation.
 *
 *   curl -X POST "http://localhost:8081/messages?text=hello"
 *   curl http://localhost:8081/messages
 */
@RestController
public class MessageController {

    private final JdbcTemplate jdbcTemplate;

    // DataSource settings come from SPRING_DATASOURCE_* environment variables.
    public MessageController(JdbcTemplate jdbcTemplate) {
        this.jdbcTemplate = jdbcTemplate;
    }

    @PostMapping("/messages")
    public String addMessage(@RequestParam String text) {
        jdbcTemplate.update(
            "INSERT INTO messages (content) VALUES (?)",
            text
        );
        return "Message saved!";
    }

    @GetMapping("/messages")
    public List<Map<String, Object>> getMessages() {
        return jdbcTemplate.queryForList(
            "SELECT id, content FROM messages ORDER BY id"
        );
    }
}
