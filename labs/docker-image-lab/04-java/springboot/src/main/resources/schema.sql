-- Executed at startup (spring.sql.init.mode=always); IF NOT EXISTS makes reruns safe.
CREATE TABLE IF NOT EXISTS messages (
    id BIGSERIAL PRIMARY KEY,
    content TEXT NOT NULL
);
