{% if padding_needed %}
const int {{input_buffer.name}}_padding[2] = { {{padding.0}}, {{padding.1}} };
for (int i = 0; i < {{num_input_channels}}; i++) {
    average_pooling1d_naive_padded(&({{input_buffer.name}}[i]),
                                   {{input_width}}, {{output_buffer.name}}[i],
                                   {{kernel_size}}, {{kernel_stride}},
                                   {% if bias_buffer %}
                                   {{bias_buffer.name}}[i],
                                   {% else %}
                                   0,
                                   {% endif %}
                                   {{input_buffer.name}}_padding);
}
{% else %}
for (int i = 0; i < {{num_input_channels}}; i++) {
    average_pooling1d_naive(&({{input_buffer.name}}[i]),
                            {{input_width}}, {{output_buffer.name}}[i],
                            {{kernel_size}}, {{kernel_stride}},
                            {% if bias_buffer %}
                            {{bias_buffer.name}}[i]
                            {% else %}
                            0
                            {% endif %});
}
{% endif %}