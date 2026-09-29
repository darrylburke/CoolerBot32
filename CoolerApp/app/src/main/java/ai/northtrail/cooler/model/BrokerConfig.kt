package ai.northtrail.cooler.model

data class BrokerConfig(
    val host: String = DEFAULT_HOST,
    val port: Int = 8883,
    val username: String = "",
    val password: String = "",
    val base: String = "cooler",
) {
    val problems: List<String>
        get() = buildList {
            if (host.isBlank()) add("Host is required")
            if (port !in 1..65535) add("Port must be 1 to 65535")
            if (username.isBlank()) add("Username is required")
            if (password.isEmpty()) add("Password is required")
            val b = base.trim()
            if (b.isEmpty() || b.startsWith("/") || b.any { it == '#' || it == '+' }) {
                add("Topic base must be a plain topic, like cooler")
            }
        }

    val isUsable: Boolean get() = problems.isEmpty()

    companion object {
        const val DEFAULT_HOST = ""
    }
}
